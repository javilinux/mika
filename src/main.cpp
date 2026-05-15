#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <M5StackChan.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>
#include <driver/i2s.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <vector>

namespace {

constexpr const char* kSecretsPath = "/secrets.json";
constexpr const char* kConfigCachePath = "/cache_config.csv";
constexpr uint32_t kWifiAttemptMs = 12000;
constexpr size_t kMaxCsvBytes = 64 * 1024;
constexpr size_t kMaxPromptBytes = 12000;
constexpr uint8_t kSpeakerVolume = 255;
constexpr uint32_t kMicSampleRate = 16000;
constexpr uint32_t kMicMaxRecordMs = 12000;
constexpr uint32_t kMicMinRecordMs = 700;
constexpr uint32_t kMicTouchReleaseGraceMs = 500;
constexpr uint32_t kMicSilenceEndMs = 900;
constexpr uint32_t kMicNoSpeechTimeoutMs = 3500;
constexpr uint32_t kMicSpeechAvgThreshold = 900;
constexpr uint32_t kMicSilenceAvgThreshold = 550;
constexpr uint32_t kLiveFirstResponseTimeoutMs = 18000;
constexpr uint32_t kLivePostAudioSilenceMs = 1800;
constexpr uint32_t kTouchRestartCooldownMs = 900;
constexpr uint32_t kTouchStartHoldMs = 140;
constexpr uint32_t kTouchReleaseDebounceMs = 240;
constexpr uint32_t kTouchReleaseConfirmMs = 220;
constexpr uint32_t kScreenCheekTouchCooldownMs = 900;
constexpr bool kTouchBargeInEnabled = false;
constexpr uint32_t kBargeInArmReleaseMs = 180;
constexpr uint32_t kBargeInHoldMs = 300;
constexpr bool kVisualFaceEnabled = true;
constexpr bool kServoGesturesEnabled = true;
constexpr const char* kLocalTimezoneName = "Europe/Madrid";
constexpr const char* kLocalTimezoneRule = "CET-1CEST,M3.5.0/2,M10.5.0/3";
constexpr int kServoYawCenter = 0;
constexpr int kServoPitchCenter = 80;
constexpr int kServoPitchListening = 160;
constexpr int kServoYawThinking = -50;
constexpr int kServoPitchThinking = 120;
constexpr int kServoPitchError = 60;
constexpr int kServoGestureSpeed = 180;
constexpr size_t kAudioChunkSamples = 320;  // 20 ms at 16 kHz.
constexpr size_t kMicStreamChunkSamples = 640;  // 40 ms at 16 kHz.
constexpr size_t kMicPrerollMaxSamples = static_cast<size_t>(kMicSampleRate) * kMicMaxRecordMs / 1000;
constexpr size_t kPlaybackBufferSamples = 24000;  // 1 s at 24 kHz.
constexpr uint32_t kOutputSampleRate = 24000;
constexpr size_t kPlaybackPrebufferMinSamples = kOutputSampleRate * 2;  // 2 s.
constexpr size_t kPlaybackPrebufferInitialSamples = kOutputSampleRate * 3;  // 3 s.
constexpr size_t kPlaybackPrebufferMaxSamples = kOutputSampleRate * 6;  // 6 s.
constexpr size_t kPlaybackPrebufferStepSamples = kOutputSampleRate;  // 1 s.
constexpr size_t kPlaybackPrebufferTrimSamples = kOutputSampleRate / 2;  // 0.5 s.
constexpr size_t kI2sChannels = 2;
constexpr size_t kI2sWriteFrames = 480;  // 20 ms at 24 kHz.
constexpr size_t kI2sWriteWords = kI2sWriteFrames * kI2sChannels;
constexpr size_t kI2sRingFrames = kOutputSampleRate * 8;  // 8 s of stereo PCM.
constexpr size_t kI2sRingWords = kI2sRingFrames * kI2sChannels;
constexpr size_t kMaxLocalSfxBytes = 1024 * 1024;
constexpr uint8_t kAw88298I2cAddr = 0x36;
constexpr uint8_t kAw9523I2cAddr = 0x58;
constexpr bool kVerboseWssFrames = false;

struct WifiNetwork {
    String ssid;
    String password;
    String auth;
    int priority = 1000;
};

struct Secrets {
    String geminiApiKey;
    String configCsvUrl;
    std::vector<WifiNetwork> wifiNetworks;
};

struct ConfigRow {
    String section;
    String key;
    String value;
    bool enabled = false;
    int order = 0;
};

struct RuntimeConfig {
    String model = "gemini-3.1-flash-live-preview";
    String voiceName;
    String playbackMode = "i2s";
    bool useGoogleSearch = false;
    bool enableRobotTools = true;
    bool conversationMemoryEnabled = true;
    int maxAnswerSeconds = 25;
    int responseTimeoutSeconds = 90;
    int conversationTimeoutSeconds = 180;
    int conversationMaxTurns = 6;
    String systemPrompt;
};

struct ConversationTurn {
    String user;
    String assistant;
    uint32_t updatedAtMs = 0;
};

struct ConversationManager {
    std::vector<ConversationTurn> turns;
    uint32_t lastActivityMs = 0;

    void reset()
    {
        turns.clear();
        lastActivityMs = 0;
    }

    bool expired(const RuntimeConfig& config) const
    {
        if (turns.empty() || lastActivityMs == 0) {
            return false;
        }
        uint32_t timeoutMs = static_cast<uint32_t>(std::max(config.conversationTimeoutSeconds, 1)) * 1000UL;
        return millis() - lastActivityMs > timeoutMs;
    }

    void expireIfNeeded(const RuntimeConfig& config)
    {
        if (!config.conversationMemoryEnabled || expired(config)) {
            reset();
        }
    }

    String cleanText(String text, size_t maxLen) const
    {
        text.replace("\r", " ");
        text.replace("\n", " ");
        text.trim();
        if (text.length() > maxLen) {
            text.remove(maxLen);
            text += "...";
        }
        return text;
    }

    bool resetRequested(String userText) const
    {
        userText.toLowerCase();
        return userText.indexOf("empecemos de nuevo") >= 0 ||
               userText.indexOf("empieza de nuevo") >= 0 ||
               userText.indexOf("olvida lo anterior") >= 0 ||
               userText.indexOf("borra el contexto") >= 0 ||
               userText.indexOf("nueva conversacion") >= 0 ||
               userText.indexOf("cambiemos de tema") >= 0;
    }

    String buildContext(const RuntimeConfig& config)
    {
        expireIfNeeded(config);
        if (!config.conversationMemoryEnabled || turns.empty()) {
            return "";
        }

        String context;
        context.reserve(768 + turns.size() * 320);
        context += "\n\nCONTEXTO RECIENTE DE ESTA CONVERSACION:\n";
        context += "- Usalo solo para entender referencias como eso, lo anterior, el, ella o despues.\n";
        context += "- No repitas el contexto salvo que el usuario lo pida.\n";
        context += "- Si el usuario pide empezar de nuevo o cambia claramente de tema, ignora este contexto.\n\n";
        for (const auto& turn : turns) {
            context += "Usuario: ";
            context += turn.user;
            context += "\nMika: ";
            context += turn.assistant;
            context += "\n\n";
        }
        return context;
    }

    void addTurn(String userText, String assistantText, const RuntimeConfig& config)
    {
        if (!config.conversationMemoryEnabled) {
            reset();
            return;
        }
        expireIfNeeded(config);

        userText = cleanText(userText, 300);
        assistantText = cleanText(assistantText, 500);
        if (userText.isEmpty() || assistantText.isEmpty()) {
            return;
        }
        if (resetRequested(userText)) {
            reset();
            lastActivityMs = millis();
            return;
        }

        ConversationTurn turn;
        turn.user = userText;
        turn.assistant = assistantText;
        turn.updatedAtMs = millis();
        turns.push_back(turn);
        lastActivityMs = turn.updatedAtMs;

        int maxTurns = std::max(1, std::min(config.conversationMaxTurns, 10));
        while (turns.size() > static_cast<size_t>(maxTurns)) {
            turns.erase(turns.begin());
        }
    }
};

struct LiveResponseResult {
    bool ok = false;
    String inputTranscript;
    String outputTranscript;
    bool turnComplete = false;
    bool interruptedByBargeIn = false;
};

enum class VisualState {
    Boot,
    Idle,
    Listening,
    Thinking,
    Speaking,
    Error,
};

enum class VisualExpression {
    Neutral,
    Happy,
    Sleepy,
    Doubt,
    Surprised,
    Sad,
    Angry,
};

enum class ContentGesture {
    None,
    Nod,
    Shake,
    Tilt,
};

enum class LocalSfx {
    None,
    Ouch,
    Happy,
    Giggle,
    Dance,
};

volatile uint8_t gPlaybackMouthLevel = 0;

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint16_t>(r & 0xF8) << 8) |
           (static_cast<uint16_t>(g & 0xFC) << 3) |
           (static_cast<uint16_t>(b) >> 3);
}

class VisualEngine {
public:
    void begin()
    {
        if (!kVisualFaceEnabled || ready) {
            return;
        }

        auto& display = M5StackChan.Display();
        canvas = new M5Canvas(&display);
        if (canvas == nullptr) {
            Serial.println("WARN visual canvas alloc");
            return;
        }
        canvas->setColorDepth(16);
        if (canvas->createSprite(display.width(), display.height()) == nullptr) {
            Serial.println("WARN visual sprite alloc");
            delete canvas;
            canvas = nullptr;
            return;
        }
        width = display.width();
        height = display.height();
        ready = true;
        state = VisualState::Idle;
        setState(VisualState::Boot);
    }

    bool isReady() const
    {
        return ready;
    }

    void setState(VisualState next)
    {
        if (!ready) {
            return;
        }
        VisualExpression nextExpression = defaultExpressionForState(next);
        if (state != next || expression != nextExpression) {
            state = next;
            expression = nextExpression;
            temporaryExpressionActive = false;
            stateChangedAt = millis();
            if (state != VisualState::Speaking) {
                gPlaybackMouthLevel = 0;
            }
            update(true);
        }
    }

    void setExpression(VisualExpression next)
    {
        if (!ready) {
            return;
        }
        temporaryExpressionActive = false;
        if (expression != next) {
            expression = next;
            update(true);
        }
    }

    void setExpressionFor(VisualExpression next, uint32_t durationMs)
    {
        if (!ready) {
            return;
        }
        expression = next;
        temporaryExpressionActive = true;
        temporaryExpressionUntil = millis() + durationMs;
        update(true);
    }

    void pokeEye(int8_t side)
    {
        if (!ready || side == 0) {
            return;
        }
        expression = VisualExpression::Angry;
        temporaryExpressionActive = true;
        temporaryExpressionUntil = millis() + 950;
        squintEyeSide = side;
        squintEyeUntil = millis() + 700;
        update(true);
    }

    void showPalomaEasterEgg()
    {
        if (!ready) {
            return;
        }
        expression = VisualExpression::Happy;
        temporaryExpressionActive = true;
        temporaryExpressionUntil = millis() + 2200;
        palomaOverlayUntil = millis() + 2300;
        update(true);
    }

    void update(bool force = false)
    {
        if (!ready) {
            return;
        }
        uint32_t now = millis();
        if (temporaryExpressionActive && now >= temporaryExpressionUntil) {
            temporaryExpressionActive = false;
            expression = defaultExpressionForState(state);
            force = true;
        }
        if (squintEyeSide != 0 && now >= squintEyeUntil) {
            squintEyeSide = 0;
            force = true;
        }
        if (!force && now - lastDrawAt < 33) {
            return;
        }
        lastDrawAt = now;
        draw(now);
    }

private:
    M5Canvas* canvas = nullptr;
    bool ready = false;
    int width = 320;
    int height = 240;
    VisualState state = VisualState::Boot;
    VisualExpression expression = VisualExpression::Neutral;
    bool temporaryExpressionActive = false;
    uint32_t temporaryExpressionUntil = 0;
    int8_t squintEyeSide = 0;
    uint32_t squintEyeUntil = 0;
    uint32_t stateChangedAt = 0;
    uint32_t lastDrawAt = 0;
    uint32_t palomaOverlayUntil = 0;
    uint32_t rngState = 0;
    uint32_t nextBlinkAt = 0;
    uint32_t blinkStartedAt = 0;
    uint8_t blinkRepeatsRemaining = 0;
    bool blinkActive = false;
    float passiveGazeX = 0.0f;
    float passiveGazeY = 0.0f;
    uint32_t nextMicroSaccadeAt = 0;
    uint32_t nextLargeSaccadeAt = 0;
    float mouthSmoothed = 0.0f;

    VisualExpression defaultExpressionForState(VisualState stateValue) const
    {
        switch (stateValue) {
            case VisualState::Boot:
                return VisualExpression::Sleepy;
            case VisualState::Listening:
                return VisualExpression::Neutral;
            case VisualState::Thinking:
                return VisualExpression::Doubt;
            case VisualState::Speaking:
                return VisualExpression::Happy;
            case VisualState::Error:
                return VisualExpression::Sad;
            case VisualState::Idle:
            default:
                return VisualExpression::Neutral;
        }
    }

    uint16_t backgroundColor() const
    {
        switch (state) {
            case VisualState::Listening:
                return rgb565(22, 48, 36);
            case VisualState::Thinking:
                return rgb565(20, 32, 58);
            case VisualState::Speaking:
                return rgb565(18, 44, 54);
            case VisualState::Error:
                return rgb565(58, 18, 20);
            case VisualState::Boot:
                return rgb565(24, 24, 36);
            case VisualState::Idle:
            default:
                return rgb565(20, 34, 28);
        }
    }

    uint16_t accentColor() const
    {
        switch (state) {
            case VisualState::Listening:
                return rgb565(255, 166, 38);
            case VisualState::Thinking:
                return rgb565(68, 132, 255);
            case VisualState::Speaking:
                return rgb565(42, 220, 230);
            case VisualState::Error:
                return rgb565(255, 80, 80);
            case VisualState::Boot:
                return rgb565(160, 170, 255);
            case VisualState::Idle:
            default:
                return rgb565(72, 220, 112);
        }
    }

    void seedRandom()
    {
        if (rngState == 0) {
            rngState = static_cast<uint32_t>(millis()) ^ 0x9E3779B9u ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this));
        }
    }

    uint32_t random32()
    {
        seedRandom();
        rngState = rngState * 1664525u + 1013904223u;
        return rngState;
    }

    uint32_t randomRange(uint32_t minValue, uint32_t maxValue)
    {
        if (maxValue <= minValue) {
            return minValue;
        }
        return minValue + random32() % (maxValue - minValue + 1);
    }

    float randomFloat(float minValue, float maxValue)
    {
        float unit = static_cast<float>(random32() & 0xFFFFu) / 65535.0f;
        return minValue + unit * (maxValue - minValue);
    }

    void scheduleNextBlink(uint32_t now)
    {
        nextBlinkAt = now + randomRange(2800, 6200);
    }

    void scheduleNextGaze(uint32_t now)
    {
        nextMicroSaccadeAt = now + randomRange(180, 620);
        nextLargeSaccadeAt = now + randomRange(2200, 4700);
    }

    void updatePassiveGaze(uint32_t now)
    {
        if (nextMicroSaccadeAt == 0 || nextLargeSaccadeAt == 0) {
            scheduleNextGaze(now);
        }

        if (now >= nextLargeSaccadeAt) {
            passiveGazeX = randomFloat(-1.35f, 1.35f);
            passiveGazeY = randomFloat(-0.85f, 0.85f);
            nextLargeSaccadeAt = now + randomRange(2200, 4700);
            nextMicroSaccadeAt = now + randomRange(180, 420);
            return;
        }

        if (now >= nextMicroSaccadeAt) {
            passiveGazeX = std::max(-1.15f, std::min(1.15f, passiveGazeX + randomFloat(-0.45f, 0.45f)));
            passiveGazeY = std::max(-0.75f, std::min(0.75f, passiveGazeY + randomFloat(-0.35f, 0.35f)));
            nextMicroSaccadeAt = now + randomRange(180, 620);
        }
    }

    float blinkOpen(uint32_t now)
    {
        if (state == VisualState::Error) {
            return 0.78f;
        }

        if (nextBlinkAt == 0) {
            scheduleNextBlink(now);
        }
        if (!blinkActive && now >= nextBlinkAt) {
            blinkActive = true;
            blinkStartedAt = now;
            blinkRepeatsRemaining = randomRange(0, 99) < 18 ? 1 : 0;
        }

        if (!blinkActive) {
            return 1.0f;
        }

        uint32_t elapsed = now - blinkStartedAt;
        if (elapsed < 48) {
            return 1.0f - static_cast<float>(elapsed) / 48.0f;
        }
        if (elapsed < 92) {
            return 0.0f;
        }
        if (elapsed < 158) {
            return static_cast<float>(elapsed - 92) / 66.0f;
        }

        blinkActive = false;
        if (blinkRepeatsRemaining > 0) {
            --blinkRepeatsRemaining;
            nextBlinkAt = now + randomRange(80, 160);
        } else {
            scheduleNextBlink(now);
        }
        return 1.0f;
    }

    uint8_t quantizedMouth(uint8_t raw)
    {
        if (raw < 24) {
            return 0;
        }
        if (raw < 70) {
            return 55;
        }
        if (raw < 125) {
            return 105;
        }
        if (raw < 190) {
            return 170;
        }
        return 230;
    }

    uint8_t smoothedMouth(uint8_t raw)
    {
        uint8_t target = quantizedMouth(raw);
        float rate = target > mouthSmoothed ? 0.62f : 0.38f;
        mouthSmoothed += (static_cast<float>(target) - mouthSmoothed) * rate;
        return static_cast<uint8_t>(mouthSmoothed);
    }

    void drawThickLine(int x0, int y0, int x1, int y1, uint16_t color)
    {
        canvas->drawLine(x0, y0, x1, y1, color);
        canvas->drawLine(x0, y0 + 1, x1, y1 + 1, color);
        canvas->drawLine(x0, y0 - 1, x1, y1 - 1, color);
    }

    void drawEye(int cx, int cy, int w, int h, float open, float gazeX, float gazeY, VisualExpression face)
    {
        (void)w;
        (void)h;
        uint16_t primary = TFT_WHITE;
        uint16_t bg = TFT_BLACK;
        int r = face == VisualExpression::Surprised ? 15 : 11;
        int x = cx + static_cast<int>(gazeX * 3.0f);
        int y = cy + static_cast<int>(gazeY * 3.0f);
        if (open <= 0.18f) {
            canvas->fillRect(x - r, y - 2, r * 2, 4, primary);
            return;
        }

        canvas->fillCircle(x, y, r, primary);
        if (face == VisualExpression::Surprised) {
            canvas->fillCircle(x, y, 5, bg);
        } else if (face == VisualExpression::Happy || face == VisualExpression::Sleepy) {
            int x0 = x - r - 1;
            int y0 = y - r;
            int w0 = r * 2 + 4;
            int h0 = r + 2;
            if (face == VisualExpression::Happy) {
                y0 += r;
                canvas->fillCircle(x, y, static_cast<int>(r / 1.5f), bg);
            }
            canvas->fillRect(x0, y0, w0, h0, bg);
        } else if (face == VisualExpression::Doubt) {
            int cut = cx < width / 2 ? 4 : 7;
            canvas->fillRect(x - r - 1, y - r - 1, r * 2 + 2, cut, bg);
        } else if (face == VisualExpression::Angry || face == VisualExpression::Sad) {
            int x0 = x - r;
            int x1 = x + r;
            int y0 = y - r;
            int y2 = y;
            bool leftEye = cx < width / 2;
            bool cutRight = face == VisualExpression::Sad ? !leftEye : leftEye;
            if (cutRight) {
                canvas->fillTriangle(x0, y0, x1, y0, x1, y2, bg);
            } else {
                canvas->fillTriangle(x0, y0, x1, y0, x0, y2, bg);
            }
        }
    }

    void drawExpressionMarks(int eyeY)
    {
        if (expression == VisualExpression::Doubt) {
            canvas->fillRect(72, eyeY - 23, 32, 4, TFT_WHITE);
            drawThickLine(214, eyeY - 25, 246, eyeY - 17, TFT_WHITE);
        } else if (expression == VisualExpression::Surprised) {
            drawThickLine(70, eyeY - 28, 110, eyeY - 31, TFT_WHITE);
            drawThickLine(210, eyeY - 31, 250, eyeY - 28, TFT_WHITE);
        } else if (expression == VisualExpression::Angry) {
            drawThickLine(74, eyeY - 24, 108, eyeY - 14, TFT_WHITE);
            drawThickLine(212, eyeY - 14, 246, eyeY - 24, TFT_WHITE);
        } else if (expression == VisualExpression::Sad) {
            drawThickLine(74, eyeY - 14, 108, eyeY - 24, TFT_WHITE);
            drawThickLine(212, eyeY - 24, 246, eyeY - 14, TFT_WHITE);
        }
    }

    void drawMouth(int cx, int y, uint8_t mouth)
    {
        uint16_t primary = TFT_WHITE;
        if (state == VisualState::Speaking) {
            float openRatio = static_cast<float>(mouth) / 255.0f;
            int h = 4 + static_cast<int>(56.0f * openRatio);
            int w = 50 + static_cast<int>(40.0f * (1.0f - openRatio));
            canvas->fillRect(cx - w / 2, y - h / 2, w, h, primary);
            return;
        }

        if (expression == VisualExpression::Surprised) {
            canvas->fillCircle(cx, y + 2, 17, primary);
            canvas->fillCircle(cx, y + 2, 10, TFT_BLACK);
        } else if (state == VisualState::Thinking) {
            canvas->fillRect(cx - 22, y - 2, 44, 4, primary);
        } else if (state == VisualState::Error) {
            drawThickLine(cx - 24, y + 7, cx + 24, y - 3, primary);
        } else if (expression == VisualExpression::Happy) {
            drawThickLine(cx - 38, y - 1, cx - 12, y + 3, primary);
            canvas->fillRect(cx - 12, y + 2, 24, 4, primary);
            drawThickLine(cx + 12, y + 3, cx + 38, y - 1, primary);
        } else {
            canvas->fillRect(cx - 45, y - 2, 90, 4, primary);
        }
    }

    void drawPalomaOverlay(uint32_t now)
    {
        if (now >= palomaOverlayUntil) {
            return;
        }
        uint16_t blush = rgb565(255, 92, 108);
        uint8_t pulse = static_cast<uint8_t>((sinf(now * 0.012f) + 1.0f) * 1.0f);
        int radius = 4 + pulse;
        canvas->fillCircle(72, 143, radius, blush);
        canvas->fillCircle(92, 143, radius, blush);
        canvas->fillCircle(234, 143, radius, blush);
        canvas->fillCircle(254, 143, radius, blush);
    }

    void draw(uint32_t now)
    {
        canvas->fillScreen(TFT_BLACK);

        updatePassiveGaze(now);
        float open = blinkOpen(now);
        float gazeX = 0.0f;
        float gazeY = 0.0f;
        if (state == VisualState::Thinking) {
            gazeX = -0.8f;
            gazeY = -0.45f;
        } else if (state == VisualState::Listening) {
            gazeY = 0.15f;
        } else if (state == VisualState::Idle || state == VisualState::Boot) {
            gazeX = 0.22f * sinf(now * 0.0012f);
            gazeY = 0.18f * cosf(now * 0.0010f);
        } else if (state == VisualState::Speaking) {
            gazeX = 0.15f * sinf(now * 0.0018f);
        }
        gazeX += passiveGazeX;
        gazeY += passiveGazeY;

        int breathY = static_cast<int>(sinf(now * 0.00155f) * 2.0f);
        int eyeY = 96 + breathY;
        float leftOpen = squintEyeSide < 0 ? 0.08f : open;
        float rightOpen = squintEyeSide > 0 ? 0.08f : open;
        drawEye(90, eyeY, 22, 22, leftOpen, gazeX, gazeY, expression);
        drawEye(230, eyeY, 22, 22, rightOpen, gazeX, gazeY, expression);
        drawExpressionMarks(eyeY);

        if (state != VisualState::Speaking) {
            mouthSmoothed = 0.0f;
        }
        uint8_t mouth = state == VisualState::Speaking ? smoothedMouth(gPlaybackMouthLevel) : 0;
        drawMouth(163, 148 + breathY, mouth);
        drawPalomaOverlay(now);

        canvas->pushSprite(0, 0);
    }
};

class RobotGestures {
public:
    struct DanceFrame {
        int yaw;
        int pitch;
        int speed;
        uint32_t intervalMs;
    };

    void begin()
    {
        if (!kServoGesturesEnabled || ready) {
            return;
        }
        ready = true;
        M5StackChan.setServoPowerEnabled(true);
        M5StackChan.Motion.setAutoTorqueReleaseEnabled(true);
        M5StackChan.Motion.setAutoAngleSyncEnabled(false);
        moveTo(kServoYawCenter, kServoPitchCenter, 140);
    }

    void setState(VisualState next)
    {
        if (!ready) {
            return;
        }
        if (activeSequence != nullptr) {
            if (activeSequenceIsDance) {
                if (next != VisualState::Idle && next != VisualState::Boot) {
                    clearActiveSequence();
                } else {
                    return;
                }
            } else if (next == VisualState::Listening || next == VisualState::Thinking || next == VisualState::Error) {
                clearActiveSequence();
            } else {
                return;
            }
        }

        uint32_t now = millis();
        if (next == lastState && now - lastGestureAt < 600) {
            return;
        }
        lastState = next;

        switch (next) {
            case VisualState::Listening:
                moveTo(kServoYawCenter, kServoPitchListening, kServoGestureSpeed);
                break;
            case VisualState::Thinking:
                moveTo(kServoYawThinking, kServoPitchThinking, kServoGestureSpeed);
                break;
            case VisualState::Speaking:
            case VisualState::Idle:
            case VisualState::Boot:
                moveTo(kServoYawCenter, kServoPitchCenter, kServoGestureSpeed);
                break;
            case VisualState::Error:
                moveTo(kServoYawCenter, kServoPitchError, 120);
                break;
        }
    }

    void update()
    {
        if (!ready) {
            return;
        }
        uint32_t now = millis();
        if (activeSequence != nullptr) {
            updateActiveSequence(now);
            return;
        }
        if (pendingReturnAt > 0 && millis() >= pendingReturnAt) {
            pendingReturnAt = 0;
            moveTo(kServoYawCenter, kServoPitchCenter, kServoGestureSpeed);
        }
    }

    void performContentGesture(ContentGesture gesture)
    {
        if (!ready || activeSequence != nullptr || gesture == ContentGesture::None) {
            return;
        }
        uint32_t now = millis();
        if (now - lastContentGestureAt < 2500) {
            return;
        }
        lastContentGestureAt = now;

        switch (gesture) {
            case ContentGesture::Nod:
                startSequence(dance3Nod, sizeof(dance3Nod) / sizeof(dance3Nod[0]), false);
                break;
            case ContentGesture::Shake:
                startSequence(dance2Shake, sizeof(dance2Shake) / sizeof(dance2Shake[0]), false);
                break;
            case ContentGesture::Tilt:
                moveTo(-90, kServoPitchCenter + 60, 220);
                pendingReturnAt = now + 520;
                break;
            case ContentGesture::None:
                break;
        }
    }

    void performCheekTouch(int8_t side)
    {
        if (!ready || activeSequence != nullptr || side == 0) {
            return;
        }
        uint32_t now = millis();
        if (now - lastCheekGestureAt < kScreenCheekTouchCooldownMs) {
            return;
        }
        lastCheekGestureAt = now;
        pendingReturnAt = now + 520;
        moveTo(side < 0 ? -70 : 70, kServoPitchCenter + 30, 220);
    }

    void performEyePoke(int8_t side)
    {
        if (!ready || activeSequence != nullptr || side == 0) {
            return;
        }
        uint32_t now = millis();
        if (now - lastEyePokeAt < kScreenCheekTouchCooldownMs) {
            return;
        }
        lastEyePokeAt = now;
        pendingReturnAt = now + 620;
        moveTo(side < 0 ? 85 : -85, kServoPitchCenter - 25, 260);
    }

    void performMouthTouch()
    {
        if (!ready || activeSequence != nullptr) {
            return;
        }
        uint32_t now = millis();
        if (now - lastMouthTouchAt < kScreenCheekTouchCooldownMs) {
            return;
        }
        lastMouthTouchAt = now;
        pendingReturnAt = now + 620;
        moveTo(kServoYawCenter, kServoPitchCenter - 45, 260);
    }

    void performPalomaEasterEgg()
    {
        if (!ready || activeSequence != nullptr) {
            return;
        }
        uint32_t now = millis();
        pendingReturnAt = now + 1200;
        moveTo(-80, kServoPitchCenter + 45, 180);
    }

    bool startOfficialDance()
    {
        if (!ready || activeSequence != nullptr || millis() - lastDanceAt < 3500) {
            return false;
        }
        lastDanceAt = millis();
        return startSequence(dance1, sizeof(dance1) / sizeof(dance1[0]), true);
    }

    bool isDancing() const
    {
        return activeSequence != nullptr && activeSequenceIsDance;
    }

private:
    static constexpr DanceFrame dance1[] = {
        {0, 0, 500, 1000},
        {600, 200, 800, 500},
        {-600, 200, 800, 500},
        {600, 200, 800, 500},
        {-600, 200, 800, 500},
        {0, 800, 900, 400},
        {0, 0, 900, 400},
        {0, 800, 900, 400},
        {0, 0, 900, 400},
        {800, 700, 700, 500},
        {-800, 700, 700, 500},
        {800, 700, 700, 500},
        {-800, 700, 700, 500},
        {0, 0, 500, 1000},
    };

    static constexpr DanceFrame dance2Shake[] = {
        {300, 0, 750, 250},
        {-300, 0, 750, 250},
        {300, 0, 750, 250},
        {-300, 0, 750, 250},
        {300, 0, 750, 250},
        {-300, 0, 750, 250},
        {0, 0, 500, 1000},
    };

    static constexpr DanceFrame dance3Nod[] = {
        {0, 350, 900, 250},
        {0, 0, 900, 250},
        {0, 350, 900, 250},
        {0, 0, 900, 250},
        {0, 350, 900, 250},
        {0, 0, 900, 250},
        {0, 0, 500, 500},
    };

    bool ready = false;
    VisualState lastState = VisualState::Boot;
    uint32_t lastGestureAt = 0;
    uint32_t lastContentGestureAt = 0;
    uint32_t lastCheekGestureAt = 0;
    uint32_t lastEyePokeAt = 0;
    uint32_t lastMouthTouchAt = 0;
    uint32_t lastDanceAt = 0;
    uint32_t pendingReturnAt = 0;
    const DanceFrame* activeSequence = nullptr;
    size_t activeSequenceCount = 0;
    size_t activeSequenceIndex = 0;
    uint32_t activeSequenceNextAt = 0;
    bool activeSequenceIsDance = false;

    bool startSequence(const DanceFrame* frames, size_t count, bool isDance)
    {
        if (!ready || frames == nullptr || count == 0 || activeSequence != nullptr) {
            return false;
        }
        activeSequence = frames;
        activeSequenceCount = count;
        activeSequenceIndex = 0;
        activeSequenceIsDance = isDance;
        pendingReturnAt = 0;
        playSequenceFrame(activeSequence[0]);
        activeSequenceNextAt = millis() + activeSequence[0].intervalMs;
        return true;
    }

    void clearActiveSequence()
    {
        activeSequence = nullptr;
        activeSequenceCount = 0;
        activeSequenceIndex = 0;
        activeSequenceNextAt = 0;
        activeSequenceIsDance = false;
    }

    void updateActiveSequence(uint32_t now)
    {
        if (activeSequence == nullptr || now < activeSequenceNextAt) {
            return;
        }
        ++activeSequenceIndex;
        if (activeSequenceIndex >= activeSequenceCount) {
            clearActiveSequence();
            moveTo(kServoYawCenter, kServoPitchCenter, kServoGestureSpeed);
            return;
        }
        playSequenceFrame(activeSequence[activeSequenceIndex]);
        activeSequenceNextAt = now + activeSequence[activeSequenceIndex].intervalMs;
    }

    void playSequenceFrame(const DanceFrame& frame)
    {
        M5StackChan.setServoPowerEnabled(true);
        M5StackChan.Motion.setTorqueEnabled(true);
        M5StackChan.Motion.move(frame.yaw, frame.pitch, frame.speed);
        lastGestureAt = millis();
    }

    void moveTo(int yaw, int pitch, int speed)
    {
        if (!kServoGesturesEnabled) {
            return;
        }
        M5StackChan.setServoPowerEnabled(true);
        M5StackChan.Motion.setTorqueEnabled(true);
        M5StackChan.Motion.move(yaw, pitch, speed);
        lastGestureAt = millis();
    }
};

Secrets gSecrets;
RuntimeConfig gRuntimeConfig;
ConversationManager gConversation;
VisualEngine gVisual;
RobotGestures gGestures;
bool gReadyForVoice = false;
bool gTurnRunning = false;
bool gResponsePlaybackActive = false;
bool gBargeInRequested = false;
bool gAutoStartVoiceTurn = false;
bool gNextTurnTouchReleaseMode = false;
size_t gAdaptivePrebufferSamples = kPlaybackPrebufferInitialSamples;
uint8_t gCleanPlaybackTurns = 0;
bool gTouchLevelStartArmed = true;
uint32_t gTouchLevelActiveSince = 0;
uint32_t gNextTouchStartAllowedAt = 0;
uint32_t gTopTouchLastActiveAt = 0;
uint32_t gNextScreenCheekTouchAllowedAt = 0;
bool gScreenMouthStopRequested = false;
uint32_t gNextPalomaEasterEggAllowedAt = 0;
LocalSfx gPendingLocalSfx = LocalSfx::None;

void setStatusColor(uint8_t r, uint8_t g, uint8_t b);
void serviceRobot();
void logLine(const String& line);
void requestLocalSfx(LocalSfx sfx);
void processPendingLocalSfx();
bool reloadRuntimeConfigFromRemote();
bool handleScreenMouthTouch(uint32_t now);
bool maybeTriggerPalomaEasterEgg(String text);

void setRobotState(VisualState state)
{
    gVisual.setState(state);
    gGestures.setState(state);
    switch (state) {
        case VisualState::Listening:
            setStatusColor(40, 24, 0);
            break;
        case VisualState::Thinking:
            setStatusColor(0, 8, 40);
            break;
        case VisualState::Speaking:
            setStatusColor(0, 28, 36);
            break;
        case VisualState::Error:
            setStatusColor(40, 0, 0);
            break;
        case VisualState::Boot:
            setStatusColor(0, 0, 24);
            break;
        case VisualState::Idle:
        default:
            setStatusColor(0, 32, 0);
            break;
    }
}

bool rawTopTouchActive()
{
    if (M5StackChan.TouchSensor.isPressed()) {
        return true;
    }
    const auto& intensities = M5StackChan.TouchSensor.getIntensities();
    return intensities[0] > 0 || intensities[1] > 0 || intensities[2] > 0;
}

bool topTouchActive()
{
    uint32_t now = millis();
    if (rawTopTouchActive()) {
        gTopTouchLastActiveAt = now;
        return true;
    }
    return gTopTouchLastActiveAt != 0 && now - gTopTouchLastActiveAt <= kTouchReleaseDebounceMs;
}

bool topTouchActiveDuring(uint32_t windowMs)
{
    uint32_t deadline = millis() + windowMs;
    do {
        serviceRobot();
        if (topTouchActive()) {
            return true;
        }
        delay(10);
    } while (millis() < deadline);
    return topTouchActive();
}

bool handleScreenMouthTouch(uint32_t now)
{
    if (!gReadyForVoice || now < gNextScreenCheekTouchAllowedAt || M5.Touch.getCount() == 0) {
        return false;
    }

    const auto& touch = M5.Touch.getDetail(0);
    if (!touch.wasPressed()) {
        return false;
    }

    const int16_t width = M5StackChan.Display().width();
    const int16_t height = M5StackChan.Display().height();
    const bool mouthZone = touch.x >= (width * 36) / 100 && touch.x <= (width * 66) / 100 &&
                           touch.y >= (height * 50) / 100 && touch.y <= (height * 76) / 100;
    if (!mouthZone) {
        return false;
    }

    gVisual.setExpressionFor(VisualExpression::Surprised, 1200);
    gGestures.performMouthTouch();
    gNextScreenCheekTouchAllowedAt = now + kScreenCheekTouchCooldownMs;
    if (gTurnRunning && gResponsePlaybackActive) {
        gScreenMouthStopRequested = true;
        logLine("Screen mouth touch: stop");
    } else {
        logLine("Screen mouth touch");
    }
    return true;
}

bool handleScreenCheekTouch(uint32_t now)
{
    if (!gReadyForVoice || gTurnRunning || now < gNextScreenCheekTouchAllowedAt || M5.Touch.getCount() == 0) {
        return false;
    }

    if (handleScreenMouthTouch(now)) {
        return true;
    }

    const auto& touch = M5.Touch.getDetail(0);
    const int16_t width = M5StackChan.Display().width();
    const int16_t height = M5StackChan.Display().height();

    if (touch.wasClicked() && touch.getClickCount() >= 2) {
        const bool reloadZone = touch.y <= height / 4;
        if (reloadZone) {
            gNextScreenCheekTouchAllowedAt = now + 1500;
            logLine("Screen config reload");
            reloadRuntimeConfigFromRemote();
            return true;
        }

        const bool danceZone = touch.x >= width / 4 && touch.x <= (width * 3) / 4 && touch.y >= (height * 62) / 100;
        if (danceZone && gGestures.startOfficialDance()) {
            gVisual.setExpressionFor(VisualExpression::Happy, 11500);
            requestLocalSfx(LocalSfx::Dance);
            gNextScreenCheekTouchAllowedAt = now + 1200;
            logLine("Screen official dance start");
            return true;
        }
    }

    if (!touch.wasPressed()) {
        return false;
    }

    int8_t side = 0;
    const bool eyeY = touch.y >= 58 && touch.y <= 132;
    if (eyeY && touch.x >= 52 && touch.x <= 128) {
        side = -1;
        gVisual.pokeEye(side);
        gGestures.performEyePoke(side);
        requestLocalSfx(LocalSfx::Ouch);
        gNextScreenCheekTouchAllowedAt = now + kScreenCheekTouchCooldownMs;
        logLine("Screen eye touch: left");
        return true;
    }
    if (eyeY && touch.x >= 192 && touch.x <= 268) {
        side = 1;
        gVisual.pokeEye(side);
        gGestures.performEyePoke(side);
        requestLocalSfx(LocalSfx::Ouch);
        gNextScreenCheekTouchAllowedAt = now + kScreenCheekTouchCooldownMs;
        logLine("Screen eye touch: right");
        return true;
    }

    const bool cheekY = touch.y >= height / 4 && touch.y <= (height * 5) / 6;
    if (!cheekY) {
        return false;
    }

    if (touch.x >= 10 && touch.x <= (width * 35) / 100) {
        side = -1;
    } else if (touch.x >= (width * 65) / 100 && touch.x <= width - 10) {
        side = 1;
    } else {
        return false;
    }

    gVisual.setExpressionFor(VisualExpression::Happy, 900);
    gGestures.performCheekTouch(side);
    requestLocalSfx(LocalSfx::Happy);
    gNextScreenCheekTouchAllowedAt = now + kScreenCheekTouchCooldownMs;
    logLine(String("Screen cheek touch: ") + (side < 0 ? "left" : "right"));
    return true;
}

struct ResponseAffect {
    VisualExpression expression = VisualExpression::Happy;
    ContentGesture gesture = ContentGesture::None;
};

struct ResponseAffectRuntime {
    bool expressionApplied = false;
    bool gestureApplied = false;
    bool predictedRefusal = false;
    bool toolExpressionApplied = false;
    bool toolGestureApplied = false;
    VisualExpression lastExpression = VisualExpression::Happy;
    ContentGesture pendingToolGesture = ContentGesture::None;
};

bool containsAny(const String& text, const char* const* needles, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        if (text.indexOf(needles[i]) >= 0) {
            return true;
        }
    }
    return false;
}

bool maybeTriggerPalomaEasterEgg(String text)
{
    text.toLowerCase();
    if (text.indexOf("paloma") < 0) {
        return false;
    }
    uint32_t now = millis();
    if (now < gNextPalomaEasterEggAllowedAt) {
        return false;
    }
    gNextPalomaEasterEggAllowedAt = now + 10000;
    gVisual.showPalomaEasterEgg();
    gGestures.performPalomaEasterEgg();
    logLine("Paloma easter egg");
    return true;
}

ResponseAffect classifyResponseAffect(String text)
{
    text.toLowerCase();

    static const char* const refusalWords[] = {
        "no puedo", "no debo", "no te puedo", "no seria seguro", "no es seguro", "no voy a",
        "no te recomiendo", "no puedo ayudar", "no puedo dar", "no puedo explicar", "no ense",
        "por aqu"
    };
    static const char* const uncertaintyWords[] = {
        "no se", "no s", "no estoy seguro", "depende", "puede variar", "no tengo ese dato",
        "preguntar al equipo", "no conozco"
    };
    static const char* const riskWords[] = {
        "virus", "malware", "phishing", "ransomware", "contrasen", "privacidad", "riesgo",
        "ataque", "atacar", "robar", "estafa", "sospech", "peligro"
    };
    static const char* const warmWords[] = {
        "hola", "perfecto", "claro", "genial", "muy bien", "gracias", "encantado",
        "bienvenido", "buena pregunta"
    };
    static const char* const surpriseWords[] = {
        "vaya", "anda", "oh", "sorprendente", "curioso", "no me esperaba"
    };
    static const char* const nodWords[] = {
        "si,", "si.", "claro que si", "correcto", "exacto", "asi es", "por supuesto",
        "efectivamente"
    };

    ResponseAffect affect;
    if (containsAny(text, refusalWords, sizeof(refusalWords) / sizeof(refusalWords[0]))) {
        affect.expression = VisualExpression::Sad;
        affect.gesture = ContentGesture::Shake;
    } else if (containsAny(text, uncertaintyWords, sizeof(uncertaintyWords) / sizeof(uncertaintyWords[0]))) {
        affect.expression = VisualExpression::Doubt;
        affect.gesture = ContentGesture::Tilt;
    } else if (containsAny(text, riskWords, sizeof(riskWords) / sizeof(riskWords[0]))) {
        affect.expression = VisualExpression::Doubt;
    } else if (containsAny(text, surpriseWords, sizeof(surpriseWords) / sizeof(surpriseWords[0]))) {
        affect.expression = VisualExpression::Surprised;
    } else if (containsAny(text, warmWords, sizeof(warmWords) / sizeof(warmWords[0]))) {
        affect.expression = VisualExpression::Happy;
    }
    if (containsAny(text, nodWords, sizeof(nodWords) / sizeof(nodWords[0]))) {
        affect.gesture = ContentGesture::Nod;
    }
    return affect;
}

bool shouldSkipQueuedToolGesture(ContentGesture gesture, String outputTranscript)
{
    if (gesture != ContentGesture::Nod) {
        return false;
    }

    outputTranscript.toLowerCase();
    static const char* const greetingWords[] = {
        "hola", "bienvenido", "encantado", "soy mika", "en que puedo ayudarte",
        "que quieres saber"
    };
    static const char* const strongAffirmationWords[] = {
        "claro que si", "correcto", "exacto", "asi es", "por supuesto", "efectivamente"
    };
    return containsAny(outputTranscript, greetingWords, sizeof(greetingWords) / sizeof(greetingWords[0])) &&
           !containsAny(outputTranscript, strongAffirmationWords, sizeof(strongAffirmationWords) / sizeof(strongAffirmationWords[0]));
}

void applyQueuedToolGesture(const String& outputTranscript, bool playbackStarted, ResponseAffectRuntime& runtime)
{
    if (!playbackStarted || runtime.pendingToolGesture == ContentGesture::None || runtime.gestureApplied) {
        return;
    }
    if (runtime.pendingToolGesture != ContentGesture::Shake && outputTranscript.length() < 24) {
        return;
    }
    if (shouldSkipQueuedToolGesture(runtime.pendingToolGesture, outputTranscript)) {
        logLine("Tool robot_gesture skipped: light greeting");
        runtime.pendingToolGesture = ContentGesture::None;
        runtime.gestureApplied = true;
        return;
    }

    gGestures.performContentGesture(runtime.pendingToolGesture);
    runtime.pendingToolGesture = ContentGesture::None;
    runtime.gestureApplied = true;
}

bool looksLikeBlockedRequest(String text)
{
    text.toLowerCase();
    static const char* const blockedIntentWords[] = {
        "quiero hack", "quiero jaque", "quiero paque", "hackear", "jaquear", "paquear",
        "ayudame a hack", "como hack", "como atacar", "quiero atacar", "robar contrase",
        "robar cuenta", "crear malware", "hacer malware", "crear virus", "hacer virus",
        "ddos", "ransomware"
    };
    return containsAny(text, blockedIntentWords, sizeof(blockedIntentWords) / sizeof(blockedIntentWords[0]));
}

void applyPredictedRefusalAffect(bool playbackStarted, ResponseAffectRuntime& runtime)
{
    gVisual.setExpression(VisualExpression::Sad);
    runtime.expressionApplied = true;
    runtime.lastExpression = VisualExpression::Sad;

    if (playbackStarted && !runtime.gestureApplied) {
        gGestures.performContentGesture(ContentGesture::Shake);
        runtime.gestureApplied = true;
    }
}

void applyResponseAffect(const String& outputTranscript, bool playbackStarted, ResponseAffectRuntime& runtime)
{
    if (!playbackStarted || outputTranscript.length() < 8) {
        return;
    }

    if (runtime.predictedRefusal) {
        applyPredictedRefusalAffect(playbackStarted, runtime);
        return;
    }

    applyQueuedToolGesture(outputTranscript, playbackStarted, runtime);
    ResponseAffect affect = classifyResponseAffect(outputTranscript);
    if (!runtime.toolExpressionApplied && (!runtime.expressionApplied || runtime.lastExpression != affect.expression)) {
        gVisual.setExpression(affect.expression);
        runtime.expressionApplied = true;
        runtime.lastExpression = affect.expression;
    }

    uint32_t gestureTextThreshold = affect.gesture == ContentGesture::Shake ? 12 : 60;
    if (!runtime.toolGestureApplied && !runtime.gestureApplied && affect.gesture != ContentGesture::None &&
        outputTranscript.length() >= gestureTextThreshold) {
        gGestures.performContentGesture(affect.gesture);
        runtime.gestureApplied = true;
    }
}

void logLine(const String& line)
{
    Serial.println(line);
    if (kVisualFaceEnabled && gVisual.isReady()) {
        return;
    }
    auto& display = M5StackChan.Display();
    display.println(line);
}

void setStatusColor(uint8_t r, uint8_t g, uint8_t b)
{
    M5StackChan.showRgbColor(r, g, b);
}

void serviceRobot()
{
    M5StackChan.update();
    gGestures.update();
    gVisual.update();
    handleScreenMouthTouch(millis());
}

bool requestBargeIn()
{
    if (!gBargeInRequested) {
        gBargeInRequested = true;
        gAutoStartVoiceTurn = true;
        gNextTurnTouchReleaseMode = true;
        setStatusColor(40, 24, 0);
        logLine("Barge-in requested");
    }
    return true;
}

bool checkBargeInTouch(bool& armed, uint32_t& releasedSince, uint32_t& pressedSince)
{
    if (!kTouchBargeInEnabled) {
        return false;
    }
    if (!gTurnRunning || !gResponsePlaybackActive) {
        return false;
    }

    M5StackChan.update();
    uint32_t now = millis();
    bool pressed = M5StackChan.TouchSensor.isPressed();

    if (!armed) {
        if (!pressed) {
            if (releasedSince == 0) {
                releasedSince = now;
            }
            if (now - releasedSince >= kBargeInArmReleaseMs) {
                armed = true;
                pressedSince = 0;
            }
        } else {
            releasedSince = 0;
        }
        return false;
    }

    if (!pressed) {
        pressedSince = 0;
        return false;
    }
    if (pressedSince == 0) {
        pressedSince = now;
    }
    if (now - pressedSince >= kBargeInHoldMs) {
        return requestBargeIn();
    }
    return gBargeInRequested;
}

String baseLiveInstruction(const RuntimeConfig& config)
{
    if (!config.systemPrompt.isEmpty()) {
        return config.systemPrompt;
    }
    return "Responde en espanol, breve y claro. Eres StackChan.";
}

String localTimeContext()
{
    time_t now = time(nullptr);
    if (now <= 1700000000) {
        return "";
    }

    struct tm localInfo {};
    localtime_r(&now, &localInfo);
    char buf[48];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &localInfo);

    String context;
    context.reserve(180);
    context += "\n\nCONTEXTO TEMPORAL:\n";
    context += "- Fecha y hora local de Malaga ahora: ";
    context += buf;
    context += ".\n";
    context += "- Usa siempre la zona horaria ";
    context += kLocalTimezoneName;
    context += " para la hora local. No uses UTC salvo que el usuario lo pida explicitamente.\n";
    return context;
}

String buildLiveInstruction(const RuntimeConfig& config, bool includeConversation)
{
    String instruction = baseLiveInstruction(config);
    instruction += localTimeContext();
    if (includeConversation) {
        String context = gConversation.buildContext(config);
        if (!context.isEmpty()) {
            logLine(String("Conversation context turns: ") + gConversation.turns.size());
            instruction += context;
        }
    }
    return instruction;
}

String readHttpLine(WiFiClientSecure& client, uint32_t timeoutMs)
{
    String line;
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        while (client.available()) {
            char c = static_cast<char>(client.read());
            line += c;
            if (c == '\n') {
                return line;
            }
        }
        delay(1);
    }
    return line;
}

bool readExact(WiFiClientSecure& client, uint8_t* data, size_t len, uint32_t timeoutMs)
{
    size_t offset = 0;
    uint32_t deadline = millis() + timeoutMs;
    while (offset < len && millis() < deadline) {
        int available = client.available();
        if (available <= 0) {
            delay(1);
            continue;
        }
        size_t wanted = std::min(static_cast<size_t>(available), len - offset);
        int read = client.read(data + offset, wanted);
        if (read > 0) {
            offset += static_cast<size_t>(read);
        }
    }
    return offset == len;
}

String makeWebSocketKey()
{
    uint8_t raw[16];
    for (uint8_t& byte : raw) {
        byte = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    unsigned char encoded[32] = {};
    size_t outLen = 0;
    mbedtls_base64_encode(encoded, sizeof(encoded), &outLen, raw, sizeof(raw));
    return String(reinterpret_cast<char*>(encoded), outLen);
}

bool base64EncodeBytes(const uint8_t* data, size_t len, String& out)
{
    size_t maxEncoded = 4 * ((len + 2) / 3) + 1;
    std::vector<unsigned char> encoded(maxEncoded);
    size_t outLen = 0;
    int rc = mbedtls_base64_encode(encoded.data(), encoded.size(), &outLen, data, len);
    if (rc != 0) {
        logLine(String("Base64 encode fail: ") + rc);
        return false;
    }

    out = String(reinterpret_cast<char*>(encoded.data()), outLen);
    return true;
}

bool sendWebSocketText(WiFiClientSecure& client, const String& payload)
{
    size_t len = payload.length();
    std::vector<uint8_t> frame;
    frame.reserve(len + 16);
    frame.push_back(0x81);  // FIN + text frame

    if (len < 126) {
        frame.push_back(0x80 | static_cast<uint8_t>(len));
    } else if (len <= 0xFFFF) {
        frame.push_back(0x80 | 126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(0x80 | 127);
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<uint8_t>((static_cast<uint64_t>(len) >> shift) & 0xFF));
        }
    }

    uint8_t mask[4];
    for (uint8_t& byte : mask) {
        byte = static_cast<uint8_t>(esp_random() & 0xFF);
        frame.push_back(byte);
    }

    for (size_t i = 0; i < len; ++i) {
        frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
    }

    return client.write(frame.data(), frame.size()) == frame.size();
}

bool readWebSocketText(WiFiClientSecure& client, String& out, uint32_t timeoutMs)
{
    uint32_t deadline = millis() + timeoutMs;
    while (millis() < deadline) {
        uint8_t header[2];
        if (!readExact(client, header, sizeof(header), timeoutMs)) {
            logLine("WSS read timeout/header");
            return false;
        }

        uint8_t opcode = header[0] & 0x0F;
        bool masked = (header[1] & 0x80) != 0;
        uint64_t len = header[1] & 0x7F;

        if (len == 126) {
            uint8_t ext[2];
            if (!readExact(client, ext, sizeof(ext), timeoutMs)) {
                return false;
            }
            len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
        } else if (len == 127) {
            uint8_t ext[8];
            if (!readExact(client, ext, sizeof(ext), timeoutMs)) {
                return false;
            }
            len = 0;
            for (uint8_t byte : ext) {
                len = (len << 8) | byte;
            }
        }

        uint8_t mask[4] = {};
        if (masked && !readExact(client, mask, sizeof(mask), timeoutMs)) {
            return false;
        }

        if (len > 48 * 1024) {
            logLine(String("WSS frame too large: ") + static_cast<uint32_t>(len));
            std::vector<uint8_t> scratch(512);
            uint64_t remaining = len;
            while (remaining > 0) {
                size_t chunk = std::min<uint64_t>(remaining, scratch.size());
                if (!readExact(client, scratch.data(), chunk, timeoutMs)) {
                    return false;
                }
                remaining -= chunk;
            }
            continue;
        }

        std::vector<uint8_t> payload(static_cast<size_t>(len));
        if (len > 0 && !readExact(client, payload.data(), payload.size(), timeoutMs)) {
            return false;
        }

        if (masked) {
            for (size_t i = 0; i < payload.size(); ++i) {
                payload[i] ^= mask[i % 4];
            }
        }

        if (opcode == 0x8) {
            String reason;
            if (payload.size() > 2) {
                reason = String(reinterpret_cast<const char*>(payload.data() + 2), payload.size() - 2);
            }
            logLine(String("WSS close frame: ") + reason.substring(0, 120));
            return false;  // close
        }
        if (opcode == 0x1 || opcode == 0x2) {
            out = String(reinterpret_cast<const char*>(payload.data()), payload.size());
            if (kVerboseWssFrames) {
                logLine(String(opcode == 0x1 ? "WSS text bytes: " : "WSS binary bytes: ") + out.length());
            }
            return true;
        }
        if (opcode == 0x9) {
            // Ping frames are unlikely during this short diagnostic. Ignore and continue.
            continue;
        }
        logLine(String("WSS skip opcode: ") + opcode);
    }

    logLine("WSS read timeout");
    return false;
}

bool extractJsonStringAfter(const String& json, int start, const char* key, String& out)
{
    int keyPos = json.indexOf(key, start);
    if (keyPos < 0) {
        return false;
    }

    int colon = json.indexOf(':', keyPos + strlen(key));
    if (colon < 0) {
        return false;
    }

    int pos = colon + 1;
    while (pos < static_cast<int>(json.length()) && isspace(static_cast<unsigned char>(json[pos]))) {
        ++pos;
    }
    if (pos >= static_cast<int>(json.length()) || json[pos] != '"') {
        return false;
    }
    ++pos;

    out = "";
    out.reserve(1024);
    bool escaped = false;
    for (; pos < static_cast<int>(json.length()); ++pos) {
        char c = json[pos];
        if (escaped) {
            if (c == '/' || c == '"' || c == '\\') {
                out += c;
            } else if (c == 'n') {
                out += '\n';
            } else if (c == 'r') {
                out += '\r';
            } else if (c == 't') {
                out += '\t';
            } else {
                out += c;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            return true;
        }
        out += c;
    }

    return false;
}

bool extractInlineAudioBase64(const String& json, String& base64)
{
    int inlineDataPos = json.indexOf("\"inlineData\"");
    if (inlineDataPos < 0) {
        return false;
    }

    String mimeType;
    if (extractJsonStringAfter(json, inlineDataPos, "\"mimeType\"", mimeType)) {
        if (mimeType.indexOf("audio/pcm") < 0) {
            return false;
        }
    }

    return extractJsonStringAfter(json, inlineDataPos, "\"data\"", base64);
}

bool appendBase64Pcm16(const String& base64, std::vector<int16_t>& pcm)
{
    if (base64.isEmpty()) {
        return false;
    }

    size_t maxDecoded = ((base64.length() + 3) / 4) * 3;
    std::vector<uint8_t> decoded(maxDecoded);
    size_t decodedLen = 0;
    int rc = mbedtls_base64_decode(
        decoded.data(),
        decoded.size(),
        &decodedLen,
        reinterpret_cast<const unsigned char*>(base64.c_str()),
        base64.length());
    if (rc != 0 || decodedLen < 2) {
        logLine(String("Base64 decode fail: ") + rc);
        return false;
    }

    size_t sampleCount = decodedLen / 2;
    size_t start = pcm.size();
    pcm.resize(start + sampleCount);
    for (size_t i = 0; i < sampleCount; ++i) {
        uint16_t lo = decoded[i * 2];
        uint16_t hi = decoded[i * 2 + 1];
        pcm[start + i] = static_cast<int16_t>((hi << 8) | lo);
    }
    return true;
}

void aw88298WriteReg(uint8_t reg, uint16_t value)
{
    value = __builtin_bswap16(value);
    M5.In_I2C.writeRegister(kAw88298I2cAddr, reg, reinterpret_cast<const uint8_t*>(&value), 2, 400000);
}

void setCoreS3SpeakerAmp(bool enabled, uint32_t sampleRate)
{
    if (enabled) {
        M5.In_I2C.bitOn(kAw9523I2cAddr, 0x02, 0b00000100, 400000);
        static constexpr uint8_t rateTable[] = {4, 5, 6, 8, 10, 11, 15, 20, 22, 44};
        size_t regValue = 0;
        size_t rate = (sampleRate + 1102) / 2205;
        while (rate > rateTable[regValue] && ++regValue < sizeof(rateTable)) {
        }
        regValue |= 0x14C0;
        aw88298WriteReg(0x61, 0x0673);
        aw88298WriteReg(0x04, 0x4040);
        aw88298WriteReg(0x05, 0x0008);
        aw88298WriteReg(0x06, static_cast<uint16_t>(regValue));
        aw88298WriteReg(0x0C, 0x0064);
    } else {
        aw88298WriteReg(0x04, 0x4000);
        M5.In_I2C.bitOff(kAw9523I2cAddr, 0x02, 0b00000100, 400000);
    }
}

struct I2sRingAudioPlayer {
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t mutex = nullptr;
    SemaphoreHandle_t dataReady = nullptr;
    SemaphoreHandle_t spaceReady = nullptr;
    SemaphoreHandle_t finished = nullptr;
    i2s_port_t port = I2S_NUM_1;
    int16_t* ring = nullptr;
    size_t readIndex = 0;
    size_t writeIndex = 0;
    size_t usedWords = 0;
    size_t highWaterWords = 0;
    size_t lowWaterWords = kI2sRingWords;
    uint32_t audioChunks = 0;
    uint32_t underruns = 0;
    size_t sampleCount = 0;
    volatile bool closing = false;
    volatile bool interrupted = false;
    bool active = false;
    bool failed = false;

    static void taskEntry(void* arg)
    {
        static_cast<I2sRingAudioPlayer*>(arg)->run();
    }

    bool setupI2s()
    {
        M5.Speaker.end();
        setCoreS3SpeakerAmp(true, kOutputSampleRate);

        i2s_config_t config = {};
        config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
        config.sample_rate = kOutputSampleRate;
        config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        // CoreS3's AW88298 is configured by M5Unified for BCK mode 16*2.
        // Send real stereo frames and duplicate the mono Gemini stream into L/R.
        config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        config.intr_alloc_flags = 0;
        config.dma_buf_count = 12;
        config.dma_buf_len = 512;
        config.use_apll = false;
        config.tx_desc_auto_clear = true;
        config.fixed_mclk = 0;
        config.mclk_multiple = I2S_MCLK_MULTIPLE_128;
        config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

        esp_err_t err = i2s_driver_install(port, &config, 0, nullptr);
        if (err == ESP_ERR_INVALID_STATE) {
            i2s_driver_uninstall(port);
            err = i2s_driver_install(port, &config, 0, nullptr);
        }
        if (err != ESP_OK) {
            logLine(String("FAIL i2s_driver_install: ") + err);
            return false;
        }

        i2s_pin_config_t pins = {};
        pins.mck_io_num = I2S_PIN_NO_CHANGE;
        pins.bck_io_num = GPIO_NUM_34;
        pins.ws_io_num = GPIO_NUM_33;
        pins.data_out_num = GPIO_NUM_13;
        pins.data_in_num = I2S_PIN_NO_CHANGE;

        err = i2s_set_pin(port, &pins);
        if (err != ESP_OK) {
            logLine(String("FAIL i2s_set_pin: ") + err);
            i2s_driver_uninstall(port);
            return false;
        }

        i2s_zero_dma_buffer(port);
        i2s_start(port);
        return true;
    }

    bool allocateRing()
    {
        size_t bytes = kI2sRingWords * sizeof(int16_t);
        ring = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (ring == nullptr) {
            ring = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
        }
        if (ring == nullptr) {
            logLine("FAIL i2s ring alloc");
            return false;
        }
        memset(ring, 0, bytes);
        return true;
    }

    bool begin()
    {
        if (active) {
            return true;
        }
        mutex = xSemaphoreCreateMutex();
        dataReady = xSemaphoreCreateBinary();
        spaceReady = xSemaphoreCreateBinary();
        finished = xSemaphoreCreateBinary();
        if (mutex == nullptr || dataReady == nullptr || spaceReady == nullptr || finished == nullptr || !allocateRing() || !setupI2s()) {
            failed = true;
            cleanup();
            return false;
        }
        readIndex = 0;
        writeIndex = 0;
        usedWords = 0;
        highWaterWords = 0;
        lowWaterWords = kI2sRingWords;
        underruns = 0;
        closing = false;
        interrupted = false;

        BaseType_t created = xTaskCreatePinnedToCore(taskEntry, "i2s_audio", 4096, this, 5, &task, 0);
        if (created != pdPASS) {
            logLine("FAIL i2s task create");
            failed = true;
            i2s_stop(port);
            i2s_driver_uninstall(port);
            setCoreS3SpeakerAmp(false, kOutputSampleRate);
            cleanup();
            return false;
        }
        active = true;
        logLine("I2S stream start");
        return true;
    }

    bool appendPcm(const std::vector<int16_t>& pcm)
    {
        if (pcm.empty()) {
            return true;
        }
        if (!begin()) {
            return false;
        }
        if (interrupted) {
            return false;
        }

        size_t offset = 0;
        while (offset < pcm.size() && !interrupted) {
            size_t framesWritten = 0;
            if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
                logLine("FAIL i2s ring lock");
                failed = true;
                return false;
            }

            size_t freeWords = kI2sRingWords - usedWords;
            if (freeWords >= kI2sChannels) {
                size_t contiguousWords = kI2sRingWords - writeIndex;
                size_t frames = std::min(pcm.size() - offset, freeWords / kI2sChannels);
                frames = std::min(frames, contiguousWords / kI2sChannels);
                for (size_t i = 0; i < frames; ++i) {
                    int16_t sample = pcm[offset + i];
                    ring[writeIndex + i * kI2sChannels] = sample;
                    ring[writeIndex + i * kI2sChannels + 1] = sample;
                }
                writeIndex = (writeIndex + frames * kI2sChannels) % kI2sRingWords;
                usedWords += frames * kI2sChannels;
                highWaterWords = std::max(highWaterWords, usedWords);
                framesWritten = frames;
            }
            xSemaphoreGive(mutex);

            if (framesWritten > 0) {
                offset += framesWritten;
                xSemaphoreGive(dataReady);
                continue;
            }

            uint32_t waitStartedAt = millis();
            while (xSemaphoreTake(spaceReady, pdMS_TO_TICKS(20)) != pdTRUE) {
                serviceRobot();
                if (interrupted) {
                    return false;
                }
                if (millis() - waitStartedAt >= 3000) {
                    logLine("FAIL i2s ring full");
                    failed = true;
                    return false;
                }
            }
        }

        if (interrupted) {
            return false;
        }

        ++audioChunks;
        sampleCount += pcm.size();
        return true;
    }

    void requestStop()
    {
        if (!active || interrupted) {
            return;
        }
        interrupted = true;
        closing = true;
        if (mutex != nullptr && xSemaphoreTake(mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            usedWords = 0;
            readIndex = writeIndex;
            xSemaphoreGive(mutex);
        }
        if (dataReady != nullptr) {
            xSemaphoreGive(dataReady);
        }
        if (spaceReady != nullptr) {
            xSemaphoreGive(spaceReady);
        }
        i2s_zero_dma_buffer(port);
    }

    void run()
    {
        int16_t* out = static_cast<int16_t*>(heap_caps_malloc(kI2sWriteWords * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (out == nullptr) {
            failed = true;
            xSemaphoreGive(finished);
            vTaskDelete(nullptr);
            return;
        }

        bool sawAudio = false;
        while (!interrupted) {
            size_t filledWords = 0;
            while (filledWords < kI2sWriteWords && !interrupted) {
                size_t copiedWords = 0;
                bool doneAndEmpty = false;

                if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    if (usedWords > 0) {
                        size_t contiguousWords = kI2sRingWords - readIndex;
                        copiedWords = std::min(kI2sWriteWords - filledWords, usedWords);
                        copiedWords = std::min(copiedWords, contiguousWords);
                        memcpy(out + filledWords, ring + readIndex, copiedWords * sizeof(int16_t));
                        readIndex = (readIndex + copiedWords) % kI2sRingWords;
                        usedWords -= copiedWords;
                        if (!closing) {
                            lowWaterWords = std::min(lowWaterWords, usedWords);
                        }
                    }
                    doneAndEmpty = closing && usedWords == 0;
                    xSemaphoreGive(mutex);
                } else {
                    failed = true;
                    break;
                }

                if (copiedWords > 0) {
                    sawAudio = true;
                    filledWords += copiedWords;
                    xSemaphoreGive(spaceReady);
                    continue;
                }

                if (doneAndEmpty) {
                    break;
                }

                TickType_t waitTicks = sawAudio ? (filledWords == 0 ? pdMS_TO_TICKS(15) : pdMS_TO_TICKS(5)) : portMAX_DELAY;
                if (xSemaphoreTake(dataReady, waitTicks) != pdTRUE) {
                    ++underruns;
                    memset(out + filledWords, 0, (kI2sWriteWords - filledWords) * sizeof(int16_t));
                    filledWords = kI2sWriteWords;
                    break;
                }
            }

            if (interrupted) {
                break;
            }
            if (filledWords == 0 && closing) {
                break;
            }
            if (failed) {
                break;
            }

            if (filledWords > 0) {
                uint64_t sum = 0;
                size_t frames = filledWords / kI2sChannels;
                for (size_t i = 0; i < filledWords; i += kI2sChannels) {
                    int32_t value = out[i];
                    if (value < 0) {
                        value = -value;
                    }
                    sum += static_cast<uint32_t>(value);
                }
                uint32_t avg = frames > 0 ? static_cast<uint32_t>(sum / frames) : 0;
                gPlaybackMouthLevel = avg < 180 ? 0 : static_cast<uint8_t>(std::min<uint32_t>(255, avg / 80));
            }

            size_t bytes = filledWords * sizeof(int16_t);
            uint8_t* data = reinterpret_cast<uint8_t*>(out);
            size_t byteOffset = 0;
            while (byteOffset < bytes && !interrupted) {
                size_t written = 0;
                esp_err_t err = i2s_write(port, data + byteOffset, bytes - byteOffset, &written, pdMS_TO_TICKS(1000));
                if (err != ESP_OK || written == 0) {
                    failed = true;
                    break;
                }
                byteOffset += written;
            }
        }

        gPlaybackMouthLevel = 0;
        heap_caps_free(out);
        xSemaphoreGive(finished);
        vTaskDelete(nullptr);
    }

    bool finish()
    {
        if (!active) {
            return !failed;
        }
        closing = true;
        xSemaphoreGive(dataReady);
        uint32_t waitMs = interrupted ? 1000 : static_cast<uint32_t>(sampleCount * 1000ULL / kOutputSampleRate) + 15000;
        bool done = false;
        uint32_t startedAt = millis();
        while (millis() - startedAt < waitMs) {
            if (gScreenMouthStopRequested && !interrupted) {
                requestStop();
            }
            if (xSemaphoreTake(finished, pdMS_TO_TICKS(20)) == pdTRUE) {
                done = true;
                break;
            }
            serviceRobot();
        }

        i2s_stop(port);
        i2s_driver_uninstall(port);
        setCoreS3SpeakerAmp(false, kOutputSampleRate);
        active = false;
        cleanup();
        String status = interrupted ? "I2S stream interrupted" : done && !failed ? "I2S stream done" : "WARN I2S stream incomplete";
        status += " underruns=";
        status += underruns;
        status += " low_ms=";
        status += lowWaterMs();
        status += " high_ms=";
        status += highWaterMs();
        logLine(status);
        return done && !failed;
    }

    uint32_t highWaterMs() const
    {
        return static_cast<uint32_t>(highWaterWords * 1000ULL / (kOutputSampleRate * kI2sChannels));
    }

    uint32_t lowWaterMs() const
    {
        if (lowWaterWords == kI2sRingWords) {
            return 0;
        }
        return static_cast<uint32_t>(lowWaterWords * 1000ULL / (kOutputSampleRate * kI2sChannels));
    }

    void cleanup()
    {
        if (ring != nullptr) {
            heap_caps_free(ring);
            ring = nullptr;
        }
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
        if (dataReady != nullptr) {
            vSemaphoreDelete(dataReady);
            dataReady = nullptr;
        }
        if (spaceReady != nullptr) {
            vSemaphoreDelete(spaceReady);
            spaceReady = nullptr;
        }
        if (finished != nullptr) {
            vSemaphoreDelete(finished);
            finished = nullptr;
        }
    }
};

struct StreamingAudioPlayer {
    std::vector<int16_t> buffers[3];
    size_t current = 0;
    uint32_t queuedBuffers = 0;
    uint32_t audioChunks = 0;
    size_t sampleCount = 0;
    bool active = false;
    bool failed = false;

    void begin()
    {
        if (active) {
            return;
        }
        M5.Speaker.begin();
        M5.Speaker.setVolume(kSpeakerVolume);
        M5.Speaker.setAllChannelVolume(255);
        M5.Speaker.stop(0);
        for (auto& buffer : buffers) {
            buffer.clear();
            buffer.reserve(kPlaybackBufferSamples);
        }
        active = true;
        logLine("Audio stream start");
    }

    bool queueCurrent()
    {
        if (buffers[current].empty()) {
            return true;
        }

        uint32_t deadline = millis() + 8000;
        while (M5.Speaker.isPlaying(0) >= 2 && millis() < deadline) {
            serviceRobot();
            delay(5);
        }
        if (M5.Speaker.isPlaying(0) >= 2) {
            logLine("FAIL audio stream queue full");
            failed = true;
            return false;
        }

        bool stopCurrent = queuedBuffers == 0;
        bool queued = M5.Speaker.playRaw(
            buffers[current].data(),
            buffers[current].size(),
            24000,
            false,
            1,
            0,
            stopCurrent);
        if (!queued) {
            logLine("FAIL audio stream queue");
            failed = true;
            return false;
        }

        ++queuedBuffers;
        current = (current + 1) % 3;
        buffers[current].clear();
        return true;
    }

    bool appendPcm(const std::vector<int16_t>& pcm)
    {
        if (pcm.empty()) {
            return true;
        }
        begin();
        ++audioChunks;
        sampleCount += pcm.size();

        size_t offset = 0;
        while (offset < pcm.size()) {
            size_t available = kPlaybackBufferSamples - buffers[current].size();
            size_t count = std::min(available, pcm.size() - offset);
            buffers[current].insert(buffers[current].end(), pcm.begin() + offset, pcm.begin() + offset + count);
            offset += count;
            if (buffers[current].size() >= kPlaybackBufferSamples && !queueCurrent()) {
                return false;
            }
        }
        return true;
    }

    bool finish()
    {
        if (!active) {
            return !failed;
        }
        queueCurrent();
        uint32_t playbackMs = static_cast<uint32_t>(sampleCount * 1000ULL / 24000ULL);
        uint32_t deadline = millis() + playbackMs + 15000;
        while (M5.Speaker.isPlaying(0) && millis() < deadline) {
            if (gScreenMouthStopRequested) {
                M5.Speaker.stop(0);
                break;
            }
            serviceRobot();
            delay(10);
        }
        bool completed = !M5.Speaker.isPlaying(0);
        M5.Speaker.stop(0);
        logLine(completed ? "Audio stream done" : "WARN audio stream timeout");
        return completed && !failed;
    }
};

void playPcm24k(const std::vector<int16_t>& pcm)
{
    if (pcm.empty()) {
        logLine("Audio playback skipped: empty");
        return;
    }

    logLine(String("Audio samples: ") + pcm.size());
    M5.Speaker.begin();
    M5.Speaker.setVolume(kSpeakerVolume);
    M5.Speaker.setAllChannelVolume(255);
    M5.Speaker.stop(0);

    bool queued = M5.Speaker.playRaw(pcm.data(), pcm.size(), 24000, false, 1, 0, true);
    logLine(queued ? "OK audio queued" : "FAIL audio queued");
    uint32_t deadline = millis() + static_cast<uint32_t>((pcm.size() * 1000ULL / 24000ULL) + 5000);
    while (M5.Speaker.isPlaying(0) && millis() < deadline) {
        serviceRobot();
        delay(10);
    }
    M5.Speaker.stop(0);
    logLine("Audio playback done");
}

const char* localSfxPath(LocalSfx sfx)
{
    switch (sfx) {
        case LocalSfx::Ouch:
            return "/sfx/ouch_24k.pcm";
        case LocalSfx::Happy:
            return "/sfx/happy_mmm_24k.pcm";
        case LocalSfx::Giggle:
            return "/sfx/giggle_24k.pcm";
        case LocalSfx::Dance:
            return "/sfx/dance_loop_24k.pcm";
        case LocalSfx::None:
        default:
            return "";
    }
}

void requestLocalSfx(LocalSfx sfx)
{
    if (sfx != LocalSfx::None && gPendingLocalSfx == LocalSfx::None) {
        gPendingLocalSfx = sfx;
    }
}

bool readPcm16File(const char* path, std::vector<int16_t>& pcm)
{
    pcm.clear();
    File file = LittleFS.open(path, "r");
    if (!file) {
        logLine(String("WARN missing sfx: ") + path);
        return false;
    }

    size_t bytes = file.size();
    if (bytes < 2 || (bytes % 2) != 0 || bytes > kMaxLocalSfxBytes) {
        logLine(String("WARN invalid sfx size: ") + path);
        file.close();
        return false;
    }

    pcm.resize(bytes / 2);
    size_t read = file.read(reinterpret_cast<uint8_t*>(pcm.data()), bytes);
    file.close();
    if (read != bytes) {
        logLine(String("WARN sfx read short: ") + path);
        pcm.clear();
        return false;
    }
    return true;
}

void playLocalSfx(LocalSfx sfx)
{
    const char* path = localSfxPath(sfx);
    if (path[0] == '\0') {
        return;
    }

    std::vector<int16_t> pcm;
    if (!readPcm16File(path, pcm)) {
        return;
    }

    logLine(String("SFX play: ") + path);
    I2sRingAudioPlayer player;
    if (player.appendPcm(pcm)) {
        player.finish();
    }
}

void processPendingLocalSfx()
{
    if (gTurnRunning || gPendingLocalSfx == LocalSfx::None) {
        return;
    }

    LocalSfx sfx = gPendingLocalSfx;
    gPendingLocalSfx = LocalSfx::None;
    playLocalSfx(sfx);
}

String normalizeBool(String value)
{
    value.trim();
    value.toLowerCase();
    return value;
}

bool readFileString(const char* path, String& out)
{
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }

    out = file.readString();
    file.close();
    return true;
}

bool writeFileString(const char* path, const String& value)
{
    File file = LittleFS.open(path, "w");
    if (!file) {
        return false;
    }
    file.print(value);
    file.close();
    return true;
}

bool loadSecrets(Secrets& secrets)
{
    File file = LittleFS.open(kSecretsPath, "r");
    if (!file) {
        logLine("FAIL secrets.json not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        logLine(String("FAIL secrets JSON: ") + err.c_str());
        return false;
    }

    secrets.geminiApiKey = doc["gemini_api_key"] | "";
    secrets.configCsvUrl = doc["config_csv_url"] | "";

    JsonArray networks = doc["wifi_networks"].as<JsonArray>();
    for (JsonObject item : networks) {
        WifiNetwork network;
        network.ssid = item["ssid"] | "";
        network.password = item["password"] | "";
        network.auth = item["auth"] | "";
        network.priority = item["priority"] | 1000;
        if (network.ssid.length() > 0) {
            secrets.wifiNetworks.push_back(network);
        }
    }

    if (secrets.geminiApiKey.isEmpty() || secrets.configCsvUrl.isEmpty() || secrets.wifiNetworks.empty()) {
        logLine("FAIL secrets missing fields");
        return false;
    }

    std::sort(secrets.wifiNetworks.begin(), secrets.wifiNetworks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        return a.priority < b.priority;
    });

    logLine("OK secrets loaded");
    return true;
}

bool connectWifi(const std::vector<WifiNetwork>& networks)
{
    WiFi.mode(WIFI_STA);
    WiFi.persistent(false);
    WiFi.setSleep(false);
    WiFi.disconnect(false, false);
    delay(300);

    logLine("WiFi scan...");
    int found = WiFi.scanNetworks(false, true);
    logLine(String("WiFi visible: ") + found);
    std::vector<String> visibleSsids;
    if (found > 0) {
        visibleSsids.reserve(found);
    }
    for (int i = 0; i < found && i < 20; ++i) {
        String ssid = WiFi.SSID(i);
        visibleSsids.push_back(ssid);
        logLine(String("  ") + ssid + " RSSI=" + WiFi.RSSI(i));
    }
    for (int i = 20; i < found; ++i) {
        visibleSsids.push_back(WiFi.SSID(i));
    }
    WiFi.scanDelete();
    WiFi.disconnect(false, false);
    delay(300);

    int maxPasses = found >= 0 ? 2 : 1;
    for (int pass = 0; pass < maxPasses; ++pass) {
        for (const auto& network : networks) {
            if (found >= 0 && std::find(visibleSsids.begin(), visibleSsids.end(), network.ssid) == visibleSsids.end()) {
                if (pass == 0) {
                    logLine(String("WiFi skip not visible: ") + network.ssid);
                }
                continue;
            }

            logLine(String(pass == 0 ? "WiFi trying: " : "WiFi retry: ") + network.ssid);
            if (normalizeBool(network.auth) == "open" || network.password.isEmpty()) {
                WiFi.begin(network.ssid.c_str());
            } else {
                WiFi.begin(network.ssid.c_str(), network.password.c_str());
            }

            uint32_t started = millis();
            while (millis() - started < kWifiAttemptMs) {
                serviceRobot();
                if (WiFi.status() == WL_CONNECTED) {
                    logLine(String("OK WiFi: ") + network.ssid);
                    logLine(String("IP: ") + WiFi.localIP().toString());
                    return true;
                }
                delay(250);
            }

            WiFi.disconnect(false, false);
            delay(300);
        }
    }

    logLine("FAIL WiFi");
    return false;
}

bool syncClock()
{
    configTzTime(kLocalTimezoneRule, "time.google.com", "pool.ntp.org");
    uint32_t started = millis();
    while (millis() - started < 15000) {
        time_t now = time(nullptr);
        if (now > 1700000000) {
            struct tm localInfo {};
            localtime_r(&now, &localInfo);
            char buf[40];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", &localInfo);
            logLine(String("OK time local: ") + buf);
            return true;
        }
        delay(250);
    }

    logLine("FAIL time sync");
    return false;
}

bool downloadCsv(const String& url, String& csv)
{
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(20000);

    if (!http.begin(secureClient, url)) {
        logLine("FAIL HTTP begin");
        return false;
    }

    int status = http.GET();
    logLine(String("CSV HTTP: ") + status);
    if (status != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    if (contentLength > static_cast<int>(kMaxCsvBytes)) {
        logLine("FAIL CSV too large");
        http.end();
        return false;
    }

    csv = http.getString();
    http.end();

    if (csv.length() == 0 || csv.length() > kMaxCsvBytes) {
        logLine("FAIL CSV size invalid");
        return false;
    }
    if (csv.indexOf("<!DOCTYPE html") >= 0 || csv.indexOf("<html") >= 0) {
        logLine("FAIL CSV is HTML");
        return false;
    }
    if (csv.indexOf("section") < 0 || csv.indexOf("enabled") < 0 || csv.indexOf("order") < 0) {
        logLine("FAIL CSV schema hint");
        return false;
    }

    logLine(String("OK CSV bytes: ") + csv.length());
    return true;
}

std::vector<String> parseCsvLine(const String& line)
{
    std::vector<String> fields;
    String field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (inQuotes && c == '"' && i + 1 < line.length() && line[i + 1] == '"') {
            field += '"';
            ++i;
        } else if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(field);
            field = "";
        } else {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

bool parseConfigCsv(const String& csv, std::vector<ConfigRow>& rows)
{
    int start = 0;
    int lineNumber = 0;
    std::vector<String> header;

    while (start <= static_cast<int>(csv.length())) {
        int end = csv.indexOf('\n', start);
        if (end < 0) {
            end = csv.length();
        }
        String line = csv.substring(start, end);
        line.trim();
        start = end + 1;

        if (line.isEmpty()) {
            continue;
        }

        ++lineNumber;
        std::vector<String> fields = parseCsvLine(line);
        if (lineNumber == 1) {
            header = fields;
            if (header.size() != 5 || header[0] != "section" || header[1] != "key" || header[2] != "value" ||
                header[3] != "enabled" || header[4] != "order") {
                logLine("FAIL CSV header");
                return false;
            }
            continue;
        }

        if (fields.size() != 5) {
            logLine(String("FAIL CSV line fields: ") + lineNumber);
            return false;
        }

        ConfigRow row;
        row.section = fields[0];
        row.key = fields[1];
        row.value = fields[2];
        row.enabled = normalizeBool(fields[3]) == "yes" || normalizeBool(fields[3]) == "true";
        row.order = fields[4].toInt();
        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(), [](const ConfigRow& a, const ConfigRow& b) {
        return a.order < b.order;
    });

    logLine(String("OK CSV rows: ") + rows.size());
    return !rows.empty();
}

String speechText(String text)
{
    text.replace("GSEC", "yisec");
    text.replace("Gsec", "yisec");
    text.replace("gsec", "yisec");
    text.replace("GESEC", "yisec");
    text.replace("gesec", "yisec");
    return text;
}

RuntimeConfig buildRuntimeConfig(const std::vector<ConfigRow>& rows)
{
    RuntimeConfig config;
    String prompt;
    prompt.reserve(4096);
    prompt += "POLITICA FIJA DEL ROBOT:\n";
    prompt += "- Responde en espanol, con frases cortas y claras para voz.\n";
    prompt += "- No pidas datos personales, contrasenas ni informacion privada.\n";
    prompt += "- No des instrucciones ofensivas de ciberseguridad.\n";
    prompt += "- Si no sabes algo, dilo claramente.\n\n";
    prompt += "- Si te preguntan por la fecha u hora, usa siempre la hora local de Malaga, zona Europe/Madrid. No uses UTC salvo que lo pidan explicitamente.\n\n";
    prompt += "- Cuando menciones el Google Safety Engineering Center de Malaga, di siempre yisec Malaga.\n";
    prompt += "- Si ves las letras G S E C, no las leas literalmente y no digas gesec: conviertelas a la palabra yisec.\n\n";

    for (const auto& row : rows) {
        if (!row.enabled) {
            continue;
        }
        if (row.section == "settings") {
            String key = row.key;
            key.toLowerCase();
            if (key == "model") {
                config.model = row.value;
            } else if (key == "voice_name") {
                config.voiceName = row.value;
                config.voiceName.trim();
            } else if (key == "playback_mode") {
                config.playbackMode = row.value;
                config.playbackMode.trim();
                config.playbackMode.toLowerCase();
                if (config.playbackMode == "buffered") {
                    config.playbackMode = "i2s";
                }
            } else if (key == "use_google_search") {
                config.useGoogleSearch = normalizeBool(row.value) == "true" || normalizeBool(row.value) == "yes";
            } else if (key == "enable_robot_tools") {
                config.enableRobotTools = normalizeBool(row.value) == "true" || normalizeBool(row.value) == "yes";
            } else if (key == "conversation_memory_enabled") {
                config.conversationMemoryEnabled = normalizeBool(row.value) == "true" || normalizeBool(row.value) == "yes";
            } else if (key == "max_answer_seconds") {
                config.maxAnswerSeconds = row.value.toInt();
            } else if (key == "response_timeout_seconds") {
                config.responseTimeoutSeconds = row.value.toInt();
            } else if (key == "conversation_timeout_seconds") {
                config.conversationTimeoutSeconds = row.value.toInt();
            } else if (key == "conversation_max_turns") {
                config.conversationMaxTurns = row.value.toInt();
            }
            continue;
        }
        if (row.section == "prompt") {
            prompt += "- ";
            String key = row.key;
            key.toLowerCase();
            if (key == "pronunciation_gsec") {
                prompt += "Di siempre yisec, sonando yi-sec, cuando te refieras al centro. No lo deletrees y no digas gesec.";
            } else {
                prompt += speechText(row.value);
            }
            prompt += "\n";
        } else if (row.section == "faq") {
            prompt += "\nFAQ ";
            prompt += speechText(row.key);
            prompt += ":\n";
            prompt += speechText(row.value);
            prompt += "\n";
        }
        if (prompt.length() > kMaxPromptBytes) {
            prompt.remove(kMaxPromptBytes);
            prompt += "\n[Prompt truncado por limite de firmware]\n";
            break;
        }
    }

    if (config.enableRobotTools) {
        prompt += "\nCONTROL DE CARA Y GESTOS:\n";
        prompt += "- Puedes usar herramientas para cambiar mi cara o mover la cabeza.\n";
        prompt += "- Usa set_expression al inicio de una respuesta si la emocion es clara.\n";
        prompt += "- Usa robot_gesture solo cuando aporte significado claro: nod para afirmaciones explicitas, shake para negar o rechazar, tilt para duda o curiosidad.\n";
        prompt += "- No uses nod para saludos simples como hola.\n";
        prompt += "- No digas en voz alta que estas usando una herramienta.\n\n";
    }

    config.conversationTimeoutSeconds = std::max(30, std::min(config.conversationTimeoutSeconds, 1800));
    config.conversationMaxTurns = std::max(1, std::min(config.conversationMaxTurns, 10));
    config.systemPrompt = prompt;
    return config;
}

void logRuntimeConfigSummary(const RuntimeConfig& config)
{
    logLine(String("Model: ") + config.model);
    if (!config.voiceName.isEmpty()) {
        logLine(String("Voice: ") + config.voiceName);
    }
    logLine(String("Playback: ") + config.playbackMode);
    logLine(String("Robot tools: ") + (config.enableRobotTools ? "on" : "off"));
    logLine(String("Conversation memory: ") + (config.conversationMemoryEnabled ? "on" : "off") +
            " turns=" + config.conversationMaxTurns +
            " timeout_s=" + config.conversationTimeoutSeconds);
    logLine(String("Prompt bytes: ") + config.systemPrompt.length());
}

bool buildRuntimeConfigFromCsv(const String& csv, RuntimeConfig& config)
{
    std::vector<ConfigRow> rows;
    if (!parseConfigCsv(csv, rows)) {
        return false;
    }
    config = buildRuntimeConfig(rows);
    return true;
}

bool loadRuntimeConfig(bool allowCacheFallback, bool clearConversation)
{
    String csv;
    RuntimeConfig nextConfig;
    bool haveConfig = false;

    if (downloadCsv(gSecrets.configCsvUrl, csv)) {
        if (buildRuntimeConfigFromCsv(csv, nextConfig)) {
            haveConfig = true;
            if (writeFileString(kConfigCachePath, csv)) {
                logLine("OK CSV cached");
            }
        } else {
            logLine("FAIL downloaded CSV invalid");
        }
    } else {
        logLine("CSV download failed");
    }

    if (!haveConfig && allowCacheFallback) {
        logLine("Using cached CSV");
        if (!readFileString(kConfigCachePath, csv)) {
            logLine("FAIL no CSV cache");
            return false;
        }
        if (!buildRuntimeConfigFromCsv(csv, nextConfig)) {
            logLine("FAIL cached CSV invalid");
            return false;
        }
        haveConfig = true;
    }

    if (!haveConfig) {
        return false;
    }

    gRuntimeConfig = nextConfig;
    if (clearConversation) {
        gConversation.reset();
        logLine("Conversation reset after config reload");
    }
    logRuntimeConfigSummary(gRuntimeConfig);
    return true;
}

bool reloadRuntimeConfigFromRemote()
{
    if (!gReadyForVoice || gTurnRunning) {
        return false;
    }

    bool wasReady = gReadyForVoice;
    gReadyForVoice = false;
    setRobotState(VisualState::Thinking);
    logLine("Config reload start");

    if (WiFi.status() != WL_CONNECTED) {
        logLine("WiFi reconnect for config reload...");
        if (!connectWifi(gSecrets.wifiNetworks)) {
            logLine("WARN config reload WiFi failed");
            gReadyForVoice = wasReady;
            setRobotState(VisualState::Idle);
            return false;
        }
    }

    bool ok = loadRuntimeConfig(false, true);
    if (ok) {
        gVisual.setExpressionFor(VisualExpression::Happy, 900);
        logLine("OK config reload");
    } else {
        gVisual.setExpressionFor(VisualExpression::Doubt, 1200);
        logLine("WARN config reload kept previous config");
    }
    gReadyForVoice = wasReady;
    setRobotState(VisualState::Idle);
    gNextTouchStartAllowedAt = millis() + kTouchRestartCooldownMs;
    gTouchLevelStartArmed = false;
    gTouchLevelActiveSince = 0;
    logLine("Touch top for voice turn");
    return ok;
}

String configuredModelName(const RuntimeConfig& config)
{
    String model = config.model;
    if (!model.startsWith("models/")) {
        model = "models/" + model;
    }
    return model;
}

bool openGeminiLiveWebSocket(const Secrets& secrets, WiFiClientSecure& client, bool showThinking = true)
{
    const char* host = "generativelanguage.googleapis.com";
    String path = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=" + secrets.geminiApiKey;

    if (showThinking) {
        setRobotState(VisualState::Thinking);
    }
    logLine("Live WSS connect...");
    client.setInsecure();
    client.setTimeout(20000);
    if (!client.connect(host, 443)) {
        logLine("FAIL Live TLS connect");
        return false;
    }

    String wsKey = makeWebSocketKey();
    client.print(String("GET ") + path + " HTTP/1.1\r\n");
    client.print(String("Host: ") + host + "\r\n");
    client.print("Upgrade: websocket\r\n");
    client.print("Connection: Upgrade\r\n");
    client.print("Sec-WebSocket-Version: 13\r\n");
    client.print(String("Sec-WebSocket-Key: ") + wsKey + "\r\n");
    client.print("User-Agent: StackChan-GSEC-Diag/0.1\r\n");
    client.print("\r\n");

    String status = readHttpLine(client, 10000);
    if (!status.startsWith("HTTP/1.1 101")) {
        logLine(String("FAIL WSS status: ") + status.substring(0, 32));
        client.stop();
        return false;
    }

    while (true) {
        String line = readHttpLine(client, 5000);
        if (line == "\r\n" || line.length() == 0) {
            break;
        }
    }
    logLine("OK WSS upgrade");
    return true;
}

bool waitLiveSetupComplete(WiFiClientSecure& client)
{
    uint32_t deadline = millis() + 15000;
    while (millis() < deadline) {
        String data;
        if (!readWebSocketText(client, data, 5000)) {
            break;
        }
        if (data.indexOf("setupComplete") >= 0) {
            logLine("OK Live setup");
            return true;
        }
    }

    logLine("FAIL Live setup complete");
    return false;
}

bool sendLiveSetup(
    WiFiClientSecure& client,
    const RuntimeConfig& config,
    const String& instruction,
    bool includeInputTranscription,
    bool manualActivityDetection,
    bool includeRobotTools = true)
{
    JsonDocument setupDoc;
    JsonObject setup = setupDoc["setup"].to<JsonObject>();
    setup["model"] = configuredModelName(config);
    JsonObject generationConfig = setup["generationConfig"].to<JsonObject>();
    JsonArray modalities = generationConfig["responseModalities"].to<JsonArray>();
    modalities.add("AUDIO");
    JsonObject systemInstruction = setup["systemInstruction"].to<JsonObject>();
    JsonArray parts = systemInstruction["parts"].to<JsonArray>();
    JsonObject firstPart = parts.add<JsonObject>();
    firstPart["text"] = instruction;
    if (!config.voiceName.isEmpty()) {
        JsonObject speechConfig = generationConfig["speechConfig"].to<JsonObject>();
        JsonObject voiceConfig = speechConfig["voiceConfig"].to<JsonObject>();
        JsonObject prebuiltVoiceConfig = voiceConfig["prebuiltVoiceConfig"].to<JsonObject>();
        prebuiltVoiceConfig["voiceName"] = config.voiceName;
    }
    if (includeInputTranscription) {
        setup["inputAudioTranscription"].to<JsonObject>();
    }
    setup["outputAudioTranscription"].to<JsonObject>();
    if (manualActivityDetection) {
        JsonObject realtimeInputConfig = setup["realtimeInputConfig"].to<JsonObject>();
        JsonObject vad = realtimeInputConfig["automaticActivityDetection"].to<JsonObject>();
        vad["disabled"] = true;
    }
    if (includeRobotTools && config.enableRobotTools) {
        JsonArray tools = setup["tools"].to<JsonArray>();
        JsonObject robotTool = tools.add<JsonObject>();
        JsonArray declarations = robotTool["functionDeclarations"].to<JsonArray>();

        JsonObject expressionFn = declarations.add<JsonObject>();
        expressionFn["name"] = "set_expression";
        expressionFn["description"] = "Cambia la expresion facial del robot.";
        JsonObject expressionParams = expressionFn["parameters"].to<JsonObject>();
        expressionParams["type"] = "object";
        JsonObject expressionProps = expressionParams["properties"].to<JsonObject>();
        JsonObject expressionProp = expressionProps["expression"].to<JsonObject>();
        expressionProp["type"] = "string";
        expressionProp["description"] = "Expresion facial que debe mostrar el robot.";
        JsonArray expressionEnum = expressionProp["enum"].to<JsonArray>();
        expressionEnum.add("neutral");
        expressionEnum.add("happy");
        expressionEnum.add("sleepy");
        expressionEnum.add("doubt");
        expressionEnum.add("surprised");
        expressionEnum.add("sad");
        expressionEnum.add("angry");
        JsonArray expressionRequired = expressionParams["required"].to<JsonArray>();
        expressionRequired.add("expression");

        JsonObject gestureFn = declarations.add<JsonObject>();
        gestureFn["name"] = "robot_gesture";
        gestureFn["description"] = "Ejecuta un gesto corto con la cabeza del robot.";
        JsonObject gestureParams = gestureFn["parameters"].to<JsonObject>();
        gestureParams["type"] = "object";
        JsonObject gestureProps = gestureParams["properties"].to<JsonObject>();
        JsonObject gestureProp = gestureProps["gesture"].to<JsonObject>();
        gestureProp["type"] = "string";
        gestureProp["description"] = "Gesto de cabeza.";
        JsonArray gestureEnum = gestureProp["enum"].to<JsonArray>();
        gestureEnum.add("nod");
        gestureEnum.add("shake");
        gestureEnum.add("tilt");
        JsonArray gestureRequired = gestureParams["required"].to<JsonArray>();
        gestureRequired.add("gesture");
    }

    String setupPayload;
    serializeJson(setupDoc, setupPayload);
    logLine(String("Live setup bytes: ") + setupPayload.length());
    if (!sendWebSocketText(client, setupPayload)) {
        logLine("FAIL Live setup send");
        return false;
    }

    return waitLiveSetupComplete(client);
}

bool sendLiveText(WiFiClientSecure& client, const char* text)
{
    JsonDocument inputDoc;
    inputDoc["realtimeInput"]["text"] = text;
    String inputPayload;
    serializeJson(inputDoc, inputPayload);
    if (!sendWebSocketText(client, inputPayload)) {
        logLine("FAIL Live text send");
        return false;
    }
    return true;
}

bool sendLiveAudioChunk(WiFiClientSecure& client, const int16_t* samples, size_t sampleCount)
{
    String audioBase64;
    if (!base64EncodeBytes(reinterpret_cast<const uint8_t*>(samples), sampleCount * sizeof(int16_t), audioBase64)) {
        return false;
    }

    String payload;
    payload.reserve(audioBase64.length() + 96);
    payload += "{\"realtimeInput\":{\"audio\":{\"data\":\"";
    payload += audioBase64;
    payload += "\",\"mimeType\":\"audio/pcm;rate=16000\"}}}";

    if (!sendWebSocketText(client, payload)) {
        logLine("FAIL Live audio send");
        return false;
    }
    return true;
}

bool sendLiveAudioStreamEnd(WiFiClientSecure& client)
{
    if (!sendWebSocketText(client, "{\"realtimeInput\":{\"audioStreamEnd\":true}}")) {
        logLine("FAIL Live audioStreamEnd send");
        return false;
    }
    return true;
}

bool sendLiveActivityStart(WiFiClientSecure& client)
{
    if (!sendWebSocketText(client, "{\"realtimeInput\":{\"activityStart\":{}}}")) {
        logLine("FAIL Live activityStart send");
        return false;
    }
    return true;
}

bool sendLiveActivityEnd(WiFiClientSecure& client)
{
    if (!sendWebSocketText(client, "{\"realtimeInput\":{\"activityEnd\":{}}}")) {
        logLine("FAIL Live activityEnd send");
        return false;
    }
    return true;
}

bool visualExpressionFromName(String name, VisualExpression& expression)
{
    name.trim();
    name.toLowerCase();
    if (name == "neutral") {
        expression = VisualExpression::Neutral;
    } else if (name == "happy") {
        expression = VisualExpression::Happy;
    } else if (name == "sleepy") {
        expression = VisualExpression::Sleepy;
    } else if (name == "doubt" || name == "thinking" || name == "warning") {
        expression = VisualExpression::Doubt;
    } else if (name == "surprised" || name == "surprise" || name == "asombro") {
        expression = VisualExpression::Surprised;
    } else if (name == "sad") {
        expression = VisualExpression::Sad;
    } else if (name == "angry") {
        expression = VisualExpression::Angry;
    } else {
        return false;
    }
    return true;
}

bool contentGestureFromName(String name, ContentGesture& gesture)
{
    name.trim();
    name.toLowerCase();
    if (name == "nod" || name == "yes") {
        gesture = ContentGesture::Nod;
    } else if (name == "shake" || name == "no" || name == "refuse") {
        gesture = ContentGesture::Shake;
    } else if (name == "tilt" || name == "curious" || name == "doubt") {
        gesture = ContentGesture::Tilt;
    } else {
        return false;
    }
    return true;
}

bool executeRobotTool(const String& name, JsonVariantConst args, ResponseAffectRuntime& runtime, String& message)
{
    if (name == "set_expression") {
        String expressionName = args["expression"] | "";
        VisualExpression expression;
        if (!visualExpressionFromName(expressionName, expression)) {
            message = "unknown expression";
            return false;
        }
        gVisual.setExpression(expression);
        runtime.expressionApplied = true;
        runtime.toolExpressionApplied = true;
        runtime.lastExpression = expression;
        message = "expression set";
        logLine(String("Tool set_expression: ") + expressionName);
        return true;
    }

    if (name == "robot_gesture") {
        String gestureName = args["gesture"] | "";
        ContentGesture gesture;
        if (!contentGestureFromName(gestureName, gesture)) {
            message = "unknown gesture";
            return false;
        }
        runtime.pendingToolGesture = gesture;
        runtime.toolGestureApplied = true;
        message = "gesture queued";
        logLine(String("Tool robot_gesture queued: ") + gestureName);
        return true;
    }

    message = "unknown tool";
    return false;
}

bool handleLiveToolCall(WiFiClientSecure& client, const String& data, ResponseAffectRuntime& runtime)
{
    if (data.indexOf("\"toolCall\"") < 0) {
        return true;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data);
    if (err) {
        logLine(String("FAIL toolCall parse: ") + err.c_str());
        return false;
    }

    JsonArrayConst calls = doc["toolCall"]["functionCalls"].as<JsonArrayConst>();
    if (calls.isNull() || calls.size() == 0) {
        return true;
    }

    JsonDocument responseDoc;
    JsonArray responses = responseDoc["toolResponse"]["functionResponses"].to<JsonArray>();
    for (JsonObjectConst call : calls) {
        String id = call["id"] | "";
        String name = call["name"] | "";
        JsonVariantConst args = call["args"];
        String message;
        bool ok = executeRobotTool(name, args, runtime, message);

        JsonObject response = responses.add<JsonObject>();
        if (!id.isEmpty()) {
            response["id"] = id;
        }
        response["name"] = name;
        JsonObject body = response["response"].to<JsonObject>();
        body["ok"] = ok;
        body["message"] = message;
    }

    String payload;
    serializeJson(responseDoc, payload);
    logLine(String("Live toolResponse bytes: ") + payload.length());
    if (!sendWebSocketText(client, payload)) {
        logLine("FAIL Live toolResponse send");
        return false;
    }
    return true;
}

uint32_t audioSamplesToMs(size_t samples)
{
    return static_cast<uint32_t>(samples * 1000ULL / kOutputSampleRate);
}

void adjustAdaptivePrebuffer(uint32_t underruns, uint32_t audioMs)
{
    if (audioMs < 5000) {
        return;
    }

    size_t previous = gAdaptivePrebufferSamples;
    if (underruns > 0) {
        gAdaptivePrebufferSamples = std::min(kPlaybackPrebufferMaxSamples, gAdaptivePrebufferSamples + kPlaybackPrebufferStepSamples);
        gCleanPlaybackTurns = 0;
    } else {
        if (gCleanPlaybackTurns < 255) {
            ++gCleanPlaybackTurns;
        }
        if (gCleanPlaybackTurns >= 3 && gAdaptivePrebufferSamples > kPlaybackPrebufferMinSamples) {
            gAdaptivePrebufferSamples = gAdaptivePrebufferSamples > kPlaybackPrebufferMinSamples + kPlaybackPrebufferTrimSamples
                                            ? gAdaptivePrebufferSamples - kPlaybackPrebufferTrimSamples
                                            : kPlaybackPrebufferMinSamples;
            gCleanPlaybackTurns = 0;
        }
    }

    if (gAdaptivePrebufferSamples != previous) {
        logLine(String("I2S adaptive prebuffer_ms=") + audioSamplesToMs(gAdaptivePrebufferSamples));
    }
}

LiveResponseResult collectLiveResponse(WiFiClientSecure& client, uint32_t timeoutMs, const RuntimeConfig& config)
{
    LiveResponseResult result;
    setRobotState(VisualState::Thinking);
    const String& playbackMode = config.playbackMode;
    bool streamPlayback = playbackMode == "streaming" || playbackMode == "i2s";
    bool i2sPlayback = playbackMode == "i2s";
    bool previousResponsePlaybackActive = gResponsePlaybackActive;
    gResponsePlaybackActive = true;
    size_t prebufferTargetSamples = i2sPlayback ? gAdaptivePrebufferSamples : kPlaybackPrebufferInitialSamples;
    uint32_t receiveStartedAt = millis();
    uint32_t lastResponseContentAt = receiveStartedAt;
    uint32_t firstAudioMs = 0;
    uint32_t playbackStartMs = 0;
    String inputTranscript;
    String outputTranscript;
    uint32_t samplesLogged = 0;
    uint32_t audioParts = 0;
    size_t pcmSamples = 0;
    bool turnComplete = false;
    StreamingAudioPlayer player;
    I2sRingAudioPlayer i2sPlayer;
    std::vector<int16_t> responsePcm;
    if (!streamPlayback) {
        responsePcm.reserve(240000);
    }
    std::vector<int16_t> prebufferPcm;
    if (streamPlayback) {
        prebufferPcm.reserve(prebufferTargetSamples + kPlaybackBufferSamples);
    }
    bool playbackStarted = false;
    bool interruptedByBargeIn = false;
    bool stoppedByMouthTouch = false;
    bool firstResponseTimedOut = false;
    bool postAudioSilenceEnded = false;
    ResponseAffectRuntime affectRuntime;
    serviceRobot();
    bool bargeInArmed = !M5StackChan.TouchSensor.isPressed();
    uint32_t bargeInReleasedSince = bargeInArmed ? millis() : 0;
    uint32_t bargeInPressedSince = 0;
    uint32_t deadline = millis() + timeoutMs;
    auto consumeMouthStop = [&]() {
        if (!gScreenMouthStopRequested) {
            return false;
        }
        if (!playbackStarted) {
            gScreenMouthStopRequested = false;
            return false;
        }
        stoppedByMouthTouch = true;
        gScreenMouthStopRequested = false;
        if (i2sPlayback) {
            i2sPlayer.requestStop();
        } else {
            M5.Speaker.stop(0);
        }
        return true;
    };
    while (millis() < deadline) {
        while (!client.available() && millis() < deadline) {
            if (checkBargeInTouch(bargeInArmed, bargeInReleasedSince, bargeInPressedSince)) {
                interruptedByBargeIn = true;
                break;
            }
            if (consumeMouthStop()) {
                break;
            }
            if (audioParts == 0 && outputTranscript.isEmpty() &&
                millis() - receiveStartedAt >= kLiveFirstResponseTimeoutMs) {
                firstResponseTimedOut = true;
                break;
            }
            if (audioParts > 0 && playbackStarted &&
                millis() - lastResponseContentAt >= kLivePostAudioSilenceMs) {
                postAudioSilenceEnded = true;
                break;
            }
            serviceRobot();
            if (consumeMouthStop()) {
                break;
            }
            delay(10);
        }
        if (interruptedByBargeIn || stoppedByMouthTouch || firstResponseTimedOut || postAudioSilenceEnded) {
            break;
        }
        if (!client.available()) {
            break;
        }

        String data;
        if (!readWebSocketText(client, data, 8000)) {
            break;
        }
        if (samplesLogged < 4) {
            String sample;
            for (size_t i = 0; i < data.length() && i < 140; ++i) {
                char c = data[i];
                sample += (c >= 32 && c <= 126) ? c : '.';
            }
            logLine(String("WSS sample: ") + sample);
            ++samplesLogged;
        }
        if (config.enableRobotTools && !handleLiveToolCall(client, data, affectRuntime)) {
            break;
        }
        String audioBase64;
        if (extractInlineAudioBase64(data, audioBase64)) {
            std::vector<int16_t> audioChunk;
            if (appendBase64Pcm16(audioBase64, audioChunk)) {
                if (audioParts == 0) {
                    firstAudioMs = millis() - receiveStartedAt;
                }
                lastResponseContentAt = millis();
                ++audioParts;
                pcmSamples += audioChunk.size();
                if (!streamPlayback) {
                    responsePcm.insert(responsePcm.end(), audioChunk.begin(), audioChunk.end());
                } else if (!playbackStarted) {
                    prebufferPcm.insert(prebufferPcm.end(), audioChunk.begin(), audioChunk.end());
                    if (prebufferPcm.size() >= prebufferTargetSamples) {
                        playbackStarted = true;
                        playbackStartMs = millis() - receiveStartedAt;
                        gPlaybackMouthLevel = 90;
                        setRobotState(VisualState::Speaking);
                        if (affectRuntime.toolExpressionApplied) {
                            gVisual.setExpression(affectRuntime.lastExpression);
                        }
                        if (affectRuntime.predictedRefusal) {
                            applyPredictedRefusalAffect(true, affectRuntime);
                        }
                        applyResponseAffect(outputTranscript, playbackStarted, affectRuntime);
                        if (i2sPlayback) {
                            i2sPlayer.appendPcm(prebufferPcm);
                        } else {
                            player.appendPcm(prebufferPcm);
                        }
                        prebufferPcm.clear();
                    }
                } else {
                    if (i2sPlayback) {
                        i2sPlayer.appendPcm(audioChunk);
                    } else {
                        player.appendPcm(audioChunk);
                    }
                }
            }
        }
        if (playbackStarted) {
            serviceRobot();
            if (consumeMouthStop()) {
                break;
            }
        }
        if (checkBargeInTouch(bargeInArmed, bargeInReleasedSince, bargeInPressedSince)) {
            interruptedByBargeIn = true;
            if (i2sPlayback) {
                i2sPlayer.requestStop();
            } else {
                M5.Speaker.stop(0);
            }
            break;
        }
        int inputTranscriptionPos = data.indexOf("\"inputTranscription\"");
        if (inputTranscriptionPos >= 0) {
            String text;
            if (extractJsonStringAfter(data, inputTranscriptionPos, "\"text\"", text)) {
                inputTranscript += text;
                maybeTriggerPalomaEasterEgg(inputTranscript);
                if (looksLikeBlockedRequest(inputTranscript)) {
                    affectRuntime.predictedRefusal = true;
                    if (!playbackStarted) {
                        gVisual.setExpression(VisualExpression::Sad);
                    } else {
                        applyPredictedRefusalAffect(true, affectRuntime);
                    }
                }
            }
        }
        int transcriptionPos = data.indexOf("\"outputTranscription\"");
        if (transcriptionPos >= 0) {
            String text;
            if (extractJsonStringAfter(data, transcriptionPos, "\"text\"", text)) {
                outputTranscript += text;
                lastResponseContentAt = millis();
                maybeTriggerPalomaEasterEgg(outputTranscript);
                applyResponseAffect(outputTranscript, playbackStarted, affectRuntime);
            }
        }
        if (data.indexOf("\"turnComplete\"") >= 0) {
            turnComplete = true;
            break;
        }
        if (playbackStarted) {
            serviceRobot();
            if (consumeMouthStop()) {
                break;
            }
        }
        if (audioParts == 0 && outputTranscript.isEmpty() &&
            millis() - receiveStartedAt >= kLiveFirstResponseTimeoutMs) {
            firstResponseTimedOut = true;
            break;
        }
        if (audioParts > 0 && playbackStarted &&
            millis() - lastResponseContentAt >= kLivePostAudioSilenceMs) {
            postAudioSilenceEnded = true;
            break;
        }
    }

    bool playbackOk = true;
    if (interruptedByBargeIn && i2sPlayback) {
        i2sPlayer.requestStop();
    }
    if (streamPlayback && !interruptedByBargeIn && !stoppedByMouthTouch && !prebufferPcm.empty()) {
        if (!playbackStarted) {
            playbackStartMs = millis() - receiveStartedAt;
        }
        gPlaybackMouthLevel = 90;
        setRobotState(VisualState::Speaking);
        if (affectRuntime.toolExpressionApplied) {
            gVisual.setExpression(affectRuntime.lastExpression);
        }
        if (affectRuntime.predictedRefusal) {
            applyPredictedRefusalAffect(true, affectRuntime);
        }
        applyResponseAffect(outputTranscript, true, affectRuntime);
        if (i2sPlayback) {
            i2sPlayer.appendPcm(prebufferPcm);
        } else {
            player.appendPcm(prebufferPcm);
        }
        prebufferPcm.clear();
    }
    if (i2sPlayback) {
        playbackOk = i2sPlayer.finish();
        if (gScreenMouthStopRequested) {
            stoppedByMouthTouch = true;
            gScreenMouthStopRequested = false;
        }
        uint32_t audioMs = audioSamplesToMs(pcmSamples);
        logLine(String("I2S metrics prebuffer_ms=") + audioSamplesToMs(prebufferTargetSamples) +
                " first_ms=" + firstAudioMs +
                " start_ms=" + playbackStartMs +
                " audio_ms=" + audioMs +
                " low_ms=" + i2sPlayer.lowWaterMs() +
                " high_ms=" + i2sPlayer.highWaterMs());
        if (!interruptedByBargeIn) {
            adjustAdaptivePrebuffer(i2sPlayer.underruns, audioMs);
        }
    } else if (streamPlayback) {
        playbackOk = player.finish();
        if (gScreenMouthStopRequested) {
            stoppedByMouthTouch = true;
            gScreenMouthStopRequested = false;
        }
    } else if (!responsePcm.empty()) {
        playPcm24k(responsePcm);
    }
    logLine(String("Live audio chunks: ") + audioParts);
    logLine(String("Live PCM samples: ") + pcmSamples);
    if (inputTranscript.length() > 0) {
        logLine(String("Live input: ") + inputTranscript.substring(0, 100));
    }
    if (outputTranscript.length() > 0) {
        logLine(String("Live transcript: ") + outputTranscript.substring(0, 100));
    }
    if (!turnComplete) {
        logLine("Live turn ended without turnComplete");
    }
    if (firstResponseTimedOut) {
        logLine("Live first response timeout");
    }
    if (postAudioSilenceEnded) {
        logLine("Live post-audio silence end");
    }
    if (interruptedByBargeIn) {
        logLine("Live response interrupted by touch");
    }
    if (stoppedByMouthTouch) {
        logLine("Live response stopped by mouth touch");
    }

    gResponsePlaybackActive = previousResponsePlaybackActive;
    result.ok = stoppedByMouthTouch || (!interruptedByBargeIn && !firstResponseTimedOut && ((audioParts > 0 && playbackOk) || outputTranscript.length() > 0));
    result.inputTranscript = inputTranscript;
    result.outputTranscript = outputTranscript;
    result.turnComplete = turnComplete;
    result.interruptedByBargeIn = interruptedByBargeIn;
    return result;
}

bool geminiLiveAudioTurn(const Secrets& secrets, const RuntimeConfig& config, const int16_t* samples, size_t sampleCount)
{
    if (samples == nullptr || sampleCount == 0) {
        logLine("FAIL empty mic audio");
        return false;
    }

    WiFiClientSecure client;
    if (!openGeminiLiveWebSocket(secrets, client)) {
        return false;
    }

    String instruction = buildLiveInstruction(config, true);
    if (!sendLiveSetup(client, config, instruction, true, true)) {
        client.stop();
        return false;
    }

    logLine(String("Live mic samples: ") + sampleCount);
    if (!sendLiveActivityStart(client)) {
        client.stop();
        return false;
    }

    size_t offset = 0;
    uint32_t chunks = 0;
    while (offset < sampleCount) {
        size_t chunk = std::min(kAudioChunkSamples, sampleCount - offset);
        if (!sendLiveAudioChunk(client, samples + offset, chunk)) {
            client.stop();
            return false;
        }
        offset += chunk;
        ++chunks;
        serviceRobot();
        delay(static_cast<uint32_t>(chunk * 1000ULL / kMicSampleRate));
    }
    if (!sendLiveActivityEnd(client)) {
        client.stop();
        return false;
    }
    logLine(String("Live mic chunks sent: ") + chunks);

    uint32_t timeoutSeconds = static_cast<uint32_t>(std::max(config.responseTimeoutSeconds, 30));
    LiveResponseResult response = collectLiveResponse(client, timeoutSeconds * 1000UL, config);
    client.stop();

    if (response.ok) {
        gConversation.addTurn(response.inputTranscript, response.outputTranscript, config);
    }
    logLine(String("Conversation turns: ") + gConversation.turns.size());
    logLine(response.ok ? "OK Live voice turn" : "FAIL Live voice turn");
    return response.ok;
}

uint32_t computeChunkAverage(const int16_t* samples, size_t sampleCount)
{
    uint64_t sum = 0;
    for (size_t i = 0; i < sampleCount; ++i) {
        int32_t value = samples[i];
        if (value < 0) {
            value = -value;
        }
        sum += static_cast<uint32_t>(value);
    }
    return sampleCount > 0 ? static_cast<uint32_t>(sum / sampleCount) : 0;
}

bool waitMicIdleInternal(uint32_t timeoutMs, bool updateUi)
{
    uint32_t deadline = millis() + timeoutMs;
    bool sawRecording = false;
    while (millis() < deadline) {
        size_t recording = M5.Mic.isRecording();
        if (recording > 0) {
            sawRecording = true;
        } else if (sawRecording) {
            return true;
        }
        if (updateUi) {
            serviceRobot();
        }
        delay(1);
    }
    return M5.Mic.isRecording() == 0;
}

bool waitMicIdle(uint32_t timeoutMs)
{
    return waitMicIdleInternal(timeoutMs, true);
}

bool waitMicIdleFromTask(uint32_t timeoutMs)
{
    return waitMicIdleInternal(timeoutMs, false);
}

void finishMicrophone()
{
    while (M5.Mic.isRecording()) {
        serviceRobot();
        delay(1);
    }
    M5.Mic.end();
    M5.Speaker.begin();
    M5.Speaker.setVolume(kSpeakerVolume);
    M5.Speaker.setAllChannelVolume(255);
}

bool beginMicrophone()
{
    setRobotState(VisualState::Listening);
    setStatusColor(40, 24, 0);
    M5.Speaker.stop(0);
    M5.Speaker.end();
    delay(50);

    auto micConfig = M5.Mic.config();
    micConfig.sample_rate = kMicSampleRate;
    micConfig.magnification = 32;
    micConfig.noise_filter_level = 0;
    M5.Mic.config(micConfig);

    if (!M5.Mic.begin()) {
        logLine("FAIL mic begin");
        M5.Speaker.begin();
        M5.Speaker.setVolume(kSpeakerVolume);
        return false;
    }
    return true;
}

struct MicPrerollCapture {
    int16_t* samples = nullptr;
    size_t capacitySamples = 0;
    volatile size_t sampleCount = 0;
    volatile bool stopRequested = false;
    volatile bool failed = false;
    TaskHandle_t task = nullptr;
    SemaphoreHandle_t done = nullptr;
    uint32_t chunkTimeoutMs = static_cast<uint32_t>(kMicStreamChunkSamples * 1000ULL / kMicSampleRate) + 250;
};

void micPrerollTask(void* arg)
{
    auto* capture = static_cast<MicPrerollCapture*>(arg);
    while (!capture->stopRequested && capture->sampleCount + kMicStreamChunkSamples <= capture->capacitySamples) {
        int16_t* dst = capture->samples + capture->sampleCount;
        memset(dst, 0, kMicStreamChunkSamples * sizeof(int16_t));
        if (!M5.Mic.record(dst, kMicStreamChunkSamples, kMicSampleRate) || !waitMicIdleFromTask(capture->chunkTimeoutMs)) {
            capture->failed = true;
            break;
        }
        capture->sampleCount += kMicStreamChunkSamples;
    }
    xSemaphoreGive(capture->done);
    vTaskDelete(nullptr);
}

bool startMicPreroll(MicPrerollCapture& capture)
{
    capture.capacitySamples = kMicPrerollMaxSamples;
    size_t bytes = capture.capacitySamples * sizeof(int16_t);
    capture.samples = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (capture.samples == nullptr) {
        capture.samples = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    capture.done = xSemaphoreCreateBinary();
    if (capture.samples == nullptr || capture.done == nullptr) {
        logLine("FAIL mic preroll alloc");
        if (capture.samples != nullptr) {
            heap_caps_free(capture.samples);
            capture.samples = nullptr;
        }
        if (capture.done != nullptr) {
            vSemaphoreDelete(capture.done);
            capture.done = nullptr;
        }
        return false;
    }
    memset(capture.samples, 0, bytes);
    BaseType_t created = xTaskCreatePinnedToCore(micPrerollTask, "mic_preroll", 4096, &capture, 4, &capture.task, 0);
    if (created != pdPASS) {
        logLine("FAIL mic preroll task");
        heap_caps_free(capture.samples);
        capture.samples = nullptr;
        vSemaphoreDelete(capture.done);
        capture.done = nullptr;
        return false;
    }
    return true;
}

void stopMicPreroll(MicPrerollCapture& capture)
{
    capture.stopRequested = true;
    if (capture.done != nullptr) {
        xSemaphoreTake(capture.done, pdMS_TO_TICKS(2000));
    }
}

void cleanupMicPreroll(MicPrerollCapture& capture)
{
    if (capture.samples != nullptr) {
        heap_caps_free(capture.samples);
        capture.samples = nullptr;
    }
    if (capture.done != nullptr) {
        vSemaphoreDelete(capture.done);
        capture.done = nullptr;
    }
    capture.task = nullptr;
}

bool geminiLiveStreamingAudioTurn(const Secrets& secrets, const RuntimeConfig& config, bool touchReleaseModeRequested)
{
    logLine("Mic streaming: preroll");
    if (!beginMicrophone()) {
        return false;
    }

    MicPrerollCapture preroll;
    if (!startMicPreroll(preroll)) {
        finishMicrophone();
        return false;
    }

    WiFiClientSecure client;
    if (!openGeminiLiveWebSocket(secrets, client, false)) {
        stopMicPreroll(preroll);
        cleanupMicPreroll(preroll);
        finishMicrophone();
        return false;
    }

    String instruction = buildLiveInstruction(config, true);
    if (!sendLiveSetup(client, config, instruction, true, true)) {
        stopMicPreroll(preroll);
        cleanupMicPreroll(preroll);
        finishMicrophone();
        client.stop();
        return false;
    }

    if (!sendLiveActivityStart(client)) {
        stopMicPreroll(preroll);
        cleanupMicPreroll(preroll);
        finishMicrophone();
        client.stop();
        return false;
    }

    stopMicPreroll(preroll);
    serviceRobot();
    logLine(String("Mic preroll ms: ") + static_cast<uint32_t>(preroll.sampleCount * 1000ULL / kMicSampleRate));

    int16_t buffers[2][kMicStreamChunkSamples];
    memset(buffers, 0, sizeof(buffers));
    int current = 0;
    int next = 1;
    uint32_t chunkTimeoutMs = static_cast<uint32_t>(kMicStreamChunkSamples * 1000ULL / kMicSampleRate) + 250;

    bool speechDetected = false;
    bool touchReleaseMode = touchReleaseModeRequested;
    bool releasedBeforeStream = touchReleaseMode && !topTouchActiveDuring(kTouchReleaseConfirmMs);
    if (releasedBeforeStream) {
        logLine("Mic touch released before stream loop; using preroll");
    }
    bool endedByTouchRelease = false;
    bool endedBySilence = false;
    bool endedByNoSpeech = false;
    bool endedByMax = false;
    size_t totalSamples = 0;
    uint32_t chunks = 0;
    uint32_t peak = 0;
    uint64_t sum = 0;
    uint32_t startedAt = millis();
    uint32_t lastVoiceAt = startedAt;
    uint32_t touchInactiveSince = releasedBeforeStream ? startedAt : 0;
    uint32_t sendMs = 0;
    logLine(String("Mic touch mode: ") + (touchReleaseMode ? "release" : "silence"));

    auto observeSamples = [&](const int16_t* data, size_t count) {
        uint32_t chunkAvg = computeChunkAverage(data, count);
        for (size_t i = 0; i < count; ++i) {
            int32_t value = data[i];
            if (value < 0) {
                value = -value;
            }
            peak = std::max<uint32_t>(peak, static_cast<uint32_t>(value));
            sum += static_cast<uint32_t>(value);
        }
        totalSamples += count;
        if (chunkAvg >= kMicSpeechAvgThreshold) {
            speechDetected = true;
            lastVoiceAt = millis();
        }
    };

    bool currentChunkQueued = false;
    if (!releasedBeforeStream) {
        if (!M5.Mic.record(buffers[current], kMicStreamChunkSamples, kMicSampleRate)) {
            logLine("FAIL mic first stream chunk");
            cleanupMicPreroll(preroll);
            finishMicrophone();
            sendLiveActivityEnd(client);
            client.stop();
            return false;
        }
        currentChunkQueued = true;
    }

    size_t prerollOffset = 0;
    while (prerollOffset < preroll.sampleCount) {
        size_t count = std::min(kMicStreamChunkSamples, preroll.sampleCount - prerollOffset);
        uint32_t sendStart = millis();
        if (!sendLiveAudioChunk(client, preroll.samples + prerollOffset, count)) {
            logLine("FAIL Live preroll audio send");
            waitMicIdle(chunkTimeoutMs);
            cleanupMicPreroll(preroll);
            finishMicrophone();
            client.stop();
            return false;
        }
        sendMs += millis() - sendStart;
        observeSamples(preroll.samples + prerollOffset, count);
        ++chunks;
        prerollOffset += count;
    }
    cleanupMicPreroll(preroll);
    if (totalSamples > 0) {
        startedAt = millis() - static_cast<uint32_t>(totalSamples * 1000ULL / kMicSampleRate);
    }

    if (currentChunkQueued && !waitMicIdle(chunkTimeoutMs)) {
        logLine("FAIL mic first stream wait");
        finishMicrophone();
        sendLiveActivityEnd(client);
        client.stop();
        return false;
    }

    if (releasedBeforeStream) {
        endedByTouchRelease = true;
    }

    while (!releasedBeforeStream) {
        uint32_t chunkAvg = computeChunkAverage(buffers[current], kMicStreamChunkSamples);
        observeSamples(buffers[current], kMicStreamChunkSamples);
        uint32_t now = millis();
        uint32_t elapsed = now - startedAt;

        serviceRobot();
        bool touchActive = topTouchActive();
        if (!touchReleaseMode && elapsed >= kMicTouchReleaseGraceMs && touchActive) {
            touchReleaseMode = true;
        }
        if (touchActive) {
            touchInactiveSince = 0;
        } else if (touchInactiveSince == 0) {
            touchInactiveSince = now;
        }
        bool touchReleasedStable = touchInactiveSince != 0 && now - touchInactiveSince >= kTouchReleaseConfirmMs;

        bool shouldStop = false;
        if (touchReleaseMode && elapsed >= kMicMinRecordMs && touchReleasedStable) {
            endedByTouchRelease = true;
            shouldStop = true;
        } else if (!touchReleaseMode && speechDetected && elapsed >= kMicMinRecordMs && chunkAvg <= kMicSilenceAvgThreshold &&
                   now - lastVoiceAt >= kMicSilenceEndMs) {
            endedBySilence = true;
            shouldStop = true;
        } else if (!touchReleaseMode && !speechDetected && elapsed >= kMicNoSpeechTimeoutMs) {
            endedByNoSpeech = true;
            shouldStop = true;
        } else if (elapsed >= kMicMaxRecordMs) {
            endedByMax = true;
            shouldStop = true;
        }

        bool nextQueued = false;
        if (!shouldStop) {
            memset(buffers[next], 0, kMicStreamChunkSamples * sizeof(int16_t));
            nextQueued = M5.Mic.record(buffers[next], kMicStreamChunkSamples, kMicSampleRate);
            if (!nextQueued) {
                logLine("FAIL mic stream queue");
                shouldStop = true;
            }
        }

        uint32_t sendStart = millis();
        if (!sendLiveAudioChunk(client, buffers[current], kMicStreamChunkSamples)) {
            logLine("FAIL Live streaming audio send");
            if (nextQueued) {
                waitMicIdle(chunkTimeoutMs);
            }
            finishMicrophone();
            client.stop();
            return false;
        }
        sendMs += millis() - sendStart;
        ++chunks;

        if (shouldStop) {
            break;
        }
        if (!waitMicIdle(chunkTimeoutMs)) {
            logLine("FAIL mic stream wait");
            break;
        }
        std::swap(current, next);
    }

    finishMicrophone();
    setRobotState(VisualState::Thinking);
    if (!sendLiveActivityEnd(client)) {
        client.stop();
        return false;
    }

    uint32_t avg = totalSamples > 0 ? static_cast<uint32_t>(sum / totalSamples) : 0;
    String reason = endedByTouchRelease ? "touch" : endedBySilence ? "silence" : endedByNoSpeech ? "no_speech" : endedByMax ? "max" : "error";
    logLine(String("Live mic chunks streamed: ") + chunks);
    logLine(String("Mic stream ms/reason: ") + static_cast<uint32_t>(totalSamples * 1000ULL / kMicSampleRate) + "/" + reason);
    logLine(String("Mic stream send_ms: ") + sendMs);
    logLine(String("Mic peak/avg: ") + peak + "/" + avg);

    if (!speechDetected) {
        logLine("No speech detected; skipping response wait");
        client.stop();
        return true;
    }

    uint32_t timeoutSeconds = static_cast<uint32_t>(std::max(config.responseTimeoutSeconds, 30));
    LiveResponseResult response = collectLiveResponse(client, timeoutSeconds * 1000UL, config);
    client.stop();

    if (response.ok) {
        gConversation.addTurn(response.inputTranscript, response.outputTranscript, config);
    }
    logLine(String("Conversation turns: ") + gConversation.turns.size());
    logLine(response.ok ? "OK Live streaming voice turn" : "FAIL Live streaming voice turn");
    return response.ok;
}

bool recordMicrophonePcm(int16_t*& samples, size_t& sampleCount)
{
    size_t maxSampleCount = static_cast<size_t>(kMicSampleRate) * kMicMaxRecordMs / 1000;
    size_t bytes = maxSampleCount * sizeof(int16_t);
    samples = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (samples == nullptr) {
        samples = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
    }
    if (samples == nullptr) {
        logLine("FAIL mic buffer alloc");
        sampleCount = 0;
        return false;
    }
    memset(samples, 0, bytes);
    sampleCount = maxSampleCount;

    logLine("Mic recording: adaptive");
    if (!beginMicrophone()) {
        logLine("FAIL mic begin");
        heap_caps_free(samples);
        samples = nullptr;
        sampleCount = 0;
        return false;
    }

    size_t offset = 0;
    bool speechDetected = false;
    bool touchReleaseMode = false;
    bool endedByTouchRelease = false;
    bool endedBySilence = false;
    bool endedByNoSpeech = false;
    uint32_t startedAt = millis();
    uint32_t lastVoiceAt = startedAt;
    uint32_t touchInactiveSince = 0;
    uint32_t maxDeadline = startedAt + kMicMaxRecordMs;
    while (offset < maxSampleCount && millis() < maxDeadline) {
        size_t chunk = std::min(kAudioChunkSamples, maxSampleCount - offset);
        if (M5.Mic.record(samples + offset, chunk, kMicSampleRate)) {
            uint32_t chunkAvg = computeChunkAverage(samples + offset, chunk);
            offset += chunk;
            uint32_t elapsed = millis() - startedAt;
            if (chunkAvg >= kMicSpeechAvgThreshold) {
                speechDetected = true;
                lastVoiceAt = millis();
            }

            serviceRobot();
            bool touchActive = topTouchActive();
            if (elapsed >= kMicTouchReleaseGraceMs && touchActive) {
                touchReleaseMode = true;
            }
            if (touchActive) {
                touchInactiveSince = 0;
            } else if (touchInactiveSince == 0) {
                touchInactiveSince = millis();
            }
            bool touchReleasedStable = touchInactiveSince != 0 && millis() - touchInactiveSince >= kTouchReleaseConfirmMs;

            if (touchReleaseMode && elapsed >= kMicMinRecordMs && touchReleasedStable) {
                endedByTouchRelease = true;
                break;
            }
            if (!touchReleaseMode && speechDetected && elapsed >= kMicMinRecordMs && chunkAvg <= kMicSilenceAvgThreshold &&
                millis() - lastVoiceAt >= kMicSilenceEndMs) {
                endedBySilence = true;
                break;
            }
            if (!touchReleaseMode && !speechDetected && elapsed >= kMicNoSpeechTimeoutMs) {
                endedByNoSpeech = true;
                break;
            }
        } else {
            delay(1);
            serviceRobot();
        }
    }
    finishMicrophone();

    sampleCount = offset;

    uint32_t peak = 0;
    uint64_t sum = 0;
    size_t measured = 0;
    for (size_t i = 0; i < sampleCount; i += 8) {
        int32_t value = samples[i];
        if (value < 0) {
            value = -value;
        }
        peak = std::max<uint32_t>(peak, static_cast<uint32_t>(value));
        sum += static_cast<uint32_t>(value);
        ++measured;
    }
    uint32_t avg = measured > 0 ? static_cast<uint32_t>(sum / measured) : 0;
    logLine(String("OK mic samples: ") + sampleCount);
    String reason = endedByTouchRelease ? "touch" : endedBySilence ? "silence" : endedByNoSpeech ? "no_speech" : "max";
    logLine(String("Mic ms/reason: ") + static_cast<uint32_t>(sampleCount * 1000ULL / kMicSampleRate) + "/" + reason);
    logLine(String("Mic peak/avg: ") + peak + "/" + avg);
    return sampleCount > 0;
}

void runVoiceTurn(bool touchReleaseMode)
{
    if (gTurnRunning) {
        return;
    }
    gTurnRunning = true;
    gBargeInRequested = false;
    setStatusColor(40, 24, 0);
    serviceRobot();

    if (WiFi.status() != WL_CONNECTED) {
        logLine("WiFi reconnect...");
        if (!connectWifi(gSecrets.wifiNetworks)) {
            setStatusColor(40, 0, 0);
            setRobotState(VisualState::Error);
            gTurnRunning = false;
            gNextTouchStartAllowedAt = millis() + kTouchRestartCooldownMs;
            gTouchLevelStartArmed = false;
            gTouchLevelActiveSince = 0;
            return;
        }
        syncClock();
    }

    bool ok = geminiLiveStreamingAudioTurn(gSecrets, gRuntimeConfig, touchReleaseMode);
    if (gBargeInRequested) {
        setStatusColor(40, 24, 0);
        setRobotState(VisualState::Listening);
        logLine("Barge-in starting next turn");
    } else {
        setStatusColor(ok ? 0 : 40, ok ? 32 : 0, 0);
        setRobotState(ok ? VisualState::Idle : VisualState::Error);
        logLine("Touch top for voice turn");
    }
    gTurnRunning = false;
    gNextTouchStartAllowedAt = millis() + kTouchRestartCooldownMs;
    gTouchLevelStartArmed = false;
    gTouchLevelActiveSince = 0;
}

void printHeap()
{
    logLine(String("Heap free: ") + ESP.getFreeHeap());
    if (psramFound()) {
        logLine(String("PSRAM free: ") + ESP.getFreePsram());
    }
}

}  // namespace

void setup()
{
    Serial.begin(115200);
    delay(500);

    M5StackChan.begin();
    gGestures.begin();
    gVisual.begin();
    if (!kVisualFaceEnabled || !gVisual.isReady()) {
        M5StackChan.Display().setTextSize(1);
        M5StackChan.Display().setTextScroll(true);
        M5StackChan.Display().fillScreen(TFT_BLACK);
        M5StackChan.Display().setTextColor(TFT_GREENYELLOW, TFT_BLACK);
    }
    setStatusColor(0, 0, 24);

    logLine("StackChan GSEC diag");
    printHeap();

    if (!LittleFS.begin(true)) {
        logLine("FAIL LittleFS");
        setRobotState(VisualState::Error);
        setStatusColor(40, 0, 0);
        return;
    }
    logLine("OK LittleFS");

    if (!loadSecrets(gSecrets)) {
        setRobotState(VisualState::Error);
        setStatusColor(40, 0, 0);
        return;
    }

    if (!connectWifi(gSecrets.wifiNetworks)) {
        setRobotState(VisualState::Error);
        setStatusColor(40, 0, 0);
        return;
    }

    if (!syncClock()) {
        setStatusColor(40, 12, 0);
    }

    if (!loadRuntimeConfig(true, false)) {
        setRobotState(VisualState::Error);
        setStatusColor(40, 0, 0);
        return;
    }

    gReadyForVoice = true;
    setRobotState(VisualState::Idle);
    gNextTouchStartAllowedAt = millis() + kTouchRestartCooldownMs;
    gTouchLevelStartArmed = false;
    gTouchLevelActiveSince = 0;
    printHeap();
    logLine("Diag complete");
    logLine("Touch top for voice turn");
}

void loop()
{
    serviceRobot();
    uint32_t now = millis();
    handleScreenCheekTouch(now);

    bool rawTouchActive = rawTopTouchActive();
    bool touchActive = topTouchActive();
    bool cooldownActive = now < gNextTouchStartAllowedAt;
    if (!rawTouchActive) {
        gTouchLevelStartArmed = true;
        gTouchLevelActiveSince = 0;
    } else if (gTouchLevelActiveSince == 0) {
        gTouchLevelActiveSince = now;
    }
    if (cooldownActive && rawTouchActive) {
        gTouchLevelStartArmed = false;
    }

    bool touchLevelStart = !cooldownActive && gTouchLevelStartArmed && rawTouchActive && gTouchLevelActiveSince != 0 &&
                           now - gTouchLevelActiveSince >= kTouchStartHoldMs;
    bool startRequested = gAutoStartVoiceTurn || (!cooldownActive && touchLevelStart);
    if (gReadyForVoice && !gTurnRunning && startRequested) {
        bool touchReleaseMode = gAutoStartVoiceTurn
                                    ? (gNextTurnTouchReleaseMode || touchActive)
                                    : touchActive;
        gTouchLevelStartArmed = false;
        gAutoStartVoiceTurn = false;
        gNextTurnTouchReleaseMode = false;
        runVoiceTurn(touchReleaseMode);
    }
    processPendingLocalSfx();
    delay(50);
}
