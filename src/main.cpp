/*
 * Vibration Inference App - Bagian 2
 * ESP32 DevKit V1 + MPU6050
 *
 * Membaca getaran dari MPU6050, menjalankan model Edge Impulse
 * (Classification + Anomaly Detection K-means) langsung di ESP32,
 * lalu menampilkan hasilnya lewat serial.
 */

#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include <DemoVibration_inferencing.h>

Adafruit_MPU6050 mpu;

// Buffer fitur: RAW_SAMPLE_COUNT sampel x 3 axis (accX, accY, accZ)
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// Ambang anomaly: skor di atas nilai ini dianggap getaran anomali
#define ANOMALY_THRESHOLD  0.3f
// Lebar bar ASCII
#define BAR_WIDTH          20

// Cetak satu baris: label + bar ASCII + persentase
static void printBar(const char *label, float value) {
  int filled = (int)(value * BAR_WIDTH + 0.5f);

  // Label rata kiri selebar 12 karakter
  Serial.print("  ");
  Serial.print(label);
  for (int i = strlen(label); i < 12; i++) {
    Serial.print(' ');
  }

  Serial.print('|');
  for (int i = 0; i < BAR_WIDTH; i++) {
    Serial.print(i < filled ? '#' : '.');
  }
  Serial.print("| ");
  Serial.print(value * 100.0f, 1);
  Serial.println('%');
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Wire.begin();  // ESP32 default: SDA=GPIO21, SCL=GPIO22

  if (!mpu.begin()) {
    Serial.println("MPU6050 tidak terdeteksi, cek wiring!");
    while (1) {
      delay(1000);
    }
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_2_G);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

  Serial.println("Edge Impulse Inference - Deteksi Getaran");
}

void loop() {
  // 1. Kumpulkan satu window data (satuan m/s^2, sama seperti saat training)
  for (size_t i = 0; i < EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE; i += 3) {
    unsigned long start = millis();

    sensors_event_t accel, gyro, temp;
    mpu.getEvent(&accel, &gyro, &temp);

    features[i + 0] = accel.acceleration.x;
    features[i + 1] = accel.acceleration.y;
    features[i + 2] = accel.acceleration.z;

    // Jaga sampling tetap EI_CLASSIFIER_INTERVAL_MS (10 ms = 100 Hz)
    while (millis() - start < EI_CLASSIFIER_INTERVAL_MS) {
      delayMicroseconds(50);
    }
  }

  // 2. Bungkus buffer jadi signal Edge Impulse
  signal_t signal;
  numpy::signal_from_buffer(features, EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, &signal);

  // 3. Jalankan classifier
  ei_impulse_result_t result = { 0 };
  EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
  if (err != EI_IMPULSE_OK) {
    Serial.print("run_classifier gagal: ");
    Serial.println(err);
    return;
  }

  // 4. Tampilkan hasil klasifikasi
  Serial.println();
  Serial.println("========== HASIL DETEKSI ==========");

  // Cari label dengan nilai tertinggi (pemenang)
  size_t top = 0;
  for (size_t i = 1; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > result.classification[top].value) {
      top = i;
    }
  }

  // Bar per label
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    printBar(result.classification[i].label, result.classification[i].value);
  }

  Serial.println("  -----------------------------------");
  Serial.print("  >> Gerakan : ");
  Serial.print(result.classification[top].label);
  Serial.print(" (");
  Serial.print(result.classification[top].value * 100.0f, 1);
  Serial.println("%)");

#if EI_CLASSIFIER_HAS_ANOMALY
  Serial.print("  >> Status  : ");
  Serial.print(result.anomaly > ANOMALY_THRESHOLD ? "ANOMALI" : "NORMAL");
  Serial.print("  (anomaly ");
  Serial.print(result.anomaly, 2);
  Serial.println(")");
#endif

  // Nilai mentah untuk debugging
  Serial.print("  [debug] ");
  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    Serial.print(result.classification[i].label);
    Serial.print("=");
    Serial.print(result.classification[i].value, 5);
    Serial.print("  ");
  }
#if EI_CLASSIFIER_HAS_ANOMALY
  Serial.print("anomaly=");
  Serial.print(result.anomaly, 5);
#endif
  Serial.println();

  Serial.print("  [debug] timing -> DSP ");
  Serial.print(result.timing.dsp);
  Serial.print(" ms | inferensi ");
  Serial.print(result.timing.classification);
  Serial.print(" ms | anomaly ");
  Serial.print(result.timing.anomaly);
  Serial.println(" ms");

  // Pemakaian RAM
  uint32_t heapTotal = ESP.getHeapSize();
  uint32_t heapFree  = ESP.getFreeHeap();
  Serial.print("  [debug] RAM heap: terpakai ");
  Serial.print(heapTotal - heapFree);
  Serial.print(" / ");
  Serial.print(heapTotal);
  Serial.print(" byte | free ");
  Serial.print(heapFree);
  Serial.print(" | maks terpakai ");
  Serial.print(heapTotal - ESP.getMinFreeHeap());
  Serial.println(" byte");

  Serial.print("  [debug] free heap minimum: ");
  Serial.print(ESP.getMinFreeHeap());
  Serial.print(" byte | sisa stack task min: ");
  Serial.print(uxTaskGetStackHighWaterMark(NULL));
  Serial.println(" byte");

  Serial.println("===================================");
}
