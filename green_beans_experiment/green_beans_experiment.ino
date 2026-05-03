/*
 * ESP32-C3 SuperMini 綠豆光質實驗控制程式
 * 
 * 此程式控制三組 RGB LED，分別用於：
 * 1. 白光組 (White Group)
 * 2. 紅光組 (Red Group)
 * 3. 紫光組 (Purple Group - 紅+藍混光)
 * 
 * 功能：
 * - 透過 Serial Monitor 輸入數值調整各組 R,G,B 強度 (0-255)。
 * - 內建 6 小時倒數計時，時間到自動關燈。
 * - 安全提醒：請確保線路固定，避免短路。
 */

// --- 引腳定義 (Pin Definitions) ---
// 白光組 (Group A)
const int PIN_W_R = 0;
const int PIN_W_G = 1;
const int PIN_W_B = 2;

// 紅光組 (Group B)
const int PIN_R_R = 5;
const int PIN_R_G = 6;
const int PIN_R_B = 7;

// 紫光組 (Group C)
const int PIN_P_R = 8;
const int PIN_P_G = 9;
const int PIN_P_B = 10;

// 實驗設定
unsigned long experimentDuration = 6 * 60 * 60 * 1000; // 6小時 (毫秒)
unsigned long startTime = 0;
bool isRunning = false;

// CAL：Mean Gray 對齊此固定值（scale = 目標 / 量測）
const float CAL_TARGET_GRAY_MEAN = 70.0f;

// 預設值（Mean 白≈162.30、紅≈73.86、紫≈115.57，於 PWM 100/204/155；對齊目標 70：CAL:162.295,73.860,115.565）
int valWR = 20, valWG = 20, valWB = 20;    // 白光組
int valRR = 169, valRG = 0, valRB = 0;    // 紅光組
int valPR = 56, valPG = 0, valPB = 56;    // 紫光組

int clamp255(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

void applyScaleToGroup(int &r, int &g, int &b, float scale) {
  r = clamp255((int)(r * scale + 0.5f));
  g = clamp255((int)(g * scale + 0.5f));
  b = clamp255((int)(b * scale + 0.5f));
}

void autoCalibrateByMeasurement(float mW, float mR, float mP) {
  if (mW <= 0 || mR <= 0 || mP <= 0) {
    Serial.println("[CAL] 錯誤：量測值需大於 0");
    return;
  }

  float target = CAL_TARGET_GRAY_MEAN;
  float scaleW = target / mW;
  float scaleR = target / mR;
  float scaleP = target / mP;

  applyScaleToGroup(valWR, valWG, valWB, scaleW);
  applyScaleToGroup(valRR, valRG, valRB, scaleR);
  applyScaleToGroup(valPR, valPG, valPB, scaleP);

  Serial.println("[CAL] 已依量測值自動修正 PWM（Mean Gray 對齊目標）：");
  Serial.printf("  Target Mean=%.1f  Scale W=%.3f, R=%.3f, P=%.3f\n", target, scaleW, scaleR, scaleP);
  if (scaleW > 1.0f || scaleR > 1.0f || scaleP > 1.0f) {
    Serial.println("[CAL] scale>1 且 PWM 已 255 時可能無法拉到目標。");
  }
  Serial.printf("  W:%d,%d,%d\n", valWR, valWG, valWB);
  Serial.printf("  R:%d,%d,%d\n", valRR, valRG, valRB);
  Serial.printf("  P:%d,%d,%d\n", valPR, valPG, valPB);
}

void setup() {
  Serial.begin(115200);
  
  // 設定所有引腳為輸出
  int pins[] = {PIN_W_R, PIN_W_G, PIN_W_B, PIN_R_R, PIN_R_G, PIN_R_B, PIN_P_R, PIN_P_G, PIN_P_B};
  for (int p : pins) {
    pinMode(p, OUTPUT);
    analogWrite(p, 0); // 初始關閉
  }

  Serial.println("\n==============================");
  Serial.println("ESP32-C3 綠豆實驗系統啟動");
  Serial.println("==============================");
  Serial.println("指令說明：");
  Serial.println("1. 輸入 'START' 開始 6 小時倒數");
  Serial.println("2. 輸入 'W:255,255,255' 設定白光組亮度");
  Serial.println("3. 輸入 'R:255,0,0' 設定紅光組亮度");
  Serial.println("4. 輸入 'P:255,0,255' 設定紫光組亮度");
  Serial.println("5. 輸入 'CAL:220,120,180' 校正：Mean Gray 對齊目標 70 (W,R,P)");
  Serial.println("6. 輸入 'STATUS' 查看目前 PWM");
  Serial.println("7. 輸入 'STOP' 強制停止");
}

void updateLEDs() {
  if (isRunning) {
    analogWrite(PIN_W_R, valWR); analogWrite(PIN_W_G, valWG); analogWrite(PIN_W_B, valWB);
    analogWrite(PIN_R_R, valRR); analogWrite(PIN_R_G, valRG); analogWrite(PIN_R_B, valRB);
    analogWrite(PIN_P_R, valPR); analogWrite(PIN_P_G, valPG); analogWrite(PIN_P_B, valPB);
  } else {
    int pins[] = {PIN_W_R, PIN_W_G, PIN_W_B, PIN_R_R, PIN_R_G, PIN_R_B, PIN_P_R, PIN_P_G, PIN_P_B};
    for (int p : pins) analogWrite(p, 0);
  }
}

void handleSerial() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toUpperCase();

    if (cmd == "START") {
      startTime = millis();
      isRunning = true;
      Serial.println(">>> 實驗開始！計時 6 小時...");
    } else if (cmd == "STOP") {
      isRunning = false;
      Serial.println(">>> 實驗停止。");
    } else if (cmd.startsWith("W:")) {
      parseColor(cmd.substring(2), valWR, valWG, valWB);
      Serial.printf("更新白光組: %d,%d,%d\n", valWR, valWG, valWB);
    } else if (cmd.startsWith("R:")) {
      parseColor(cmd.substring(2), valRR, valRG, valRB);
      Serial.printf("更新紅光組: %d,%d,%d\n", valRR, valRG, valRB);
    } else if (cmd.startsWith("P:")) {
      parseColor(cmd.substring(2), valPR, valPG, valPB);
      Serial.printf("更新紫光組: %d,%d,%d\n", valPR, valPG, valPB);
    } else if (cmd.startsWith("CAL:")) {
      String s = cmd.substring(4);
      int f = s.indexOf(',');
      int l = s.lastIndexOf(',');
      if (f != -1 && l != -1 && f != l) {
        float mW = s.substring(0, f).toFloat();
        float mR = s.substring(f + 1, l).toFloat();
        float mP = s.substring(l + 1).toFloat();
        autoCalibrateByMeasurement(mW, mR, mP);
      } else {
        Serial.println("[CAL] 格式錯誤，請用 CAL:白光,紅光,紫光");
      }
    } else if (cmd == "STATUS") {
      Serial.println("目前 PWM：");
      Serial.printf("  W:%d,%d,%d\n", valWR, valWG, valWB);
      Serial.printf("  R:%d,%d,%d\n", valRR, valRG, valRB);
      Serial.printf("  P:%d,%d,%d\n", valPR, valPG, valPB);
    }
    updateLEDs();
  }
}

void parseColor(String s, int &r, int &g, int &b) {
  int f = s.indexOf(',');
  int l = s.lastIndexOf(',');
  if (f != -1 && l != -1) {
    r = s.substring(0, f).toInt();
    g = s.substring(f + 1, l).toInt();
    b = s.substring(l + 1).toInt();
  }
}

void loop() {
  handleSerial();

  if (isRunning) {
    unsigned long elapsed = millis() - startTime;
    if (elapsed >= experimentDuration) {
      isRunning = false;
      updateLEDs();
      Serial.println("==============================");
      Serial.println("時間到！實驗結束，LED 已關閉。");
      Serial.println("==============================");
    } else {
      // 每分鐘顯示一次剩餘時間
      static unsigned long lastUpdate = 0;
      if (millis() - lastUpdate > 60000) {
        lastUpdate = millis();
        long remaining = (experimentDuration - elapsed) / 1000 / 60;
        Serial.printf("剩餘時間: %ld 分鐘\n", remaining);
      }
    }
  }
}
