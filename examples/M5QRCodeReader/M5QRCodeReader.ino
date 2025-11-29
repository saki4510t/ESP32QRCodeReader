/*
 * ESP32QRCodeReader sample sketch for M5CoreS3
 * Copyright 2025 t_saki@serenegiant.com
 */

#include <Arduino.h>
#include <M5CoreS3.h>
#include <ESP32QRCodeReader.h>

/// M5Stack AtomS3R-CAMを使う場合はCAMERA_MODEL_M5STACK_ATOM_S3RCAMの指定に津輪得て下のコメントを解除
// #define USE_ATOMS3R_CAM

//--------------------------------------------------------------------------------
// 前方参照宣言
//--------------------------------------------------------------------------------
/**
 * ESP32QRCodeReaderのカメラ初期化時の追加設定を行うためのコールバック
 */
static int on_camera_init(camera_config_t &config);
/**
 * ESP32QRCodeReaderからのフレーム毎のコールバック関数
 */
static int on_frame(camera_fb_t *fb);

//--------------------------------------------------------------------------------
// 変数宣言
//--------------------------------------------------------------------------------
ESP32QRCodeReader reader(CAMERA_MODEL_M5STACK_CORES3, FRAMESIZE_QVGA, on_camera_init, on_frame);
int display_width = 0;
int display_height = 0;
String last_payload = "";
// オフスクリーン描画用のキャンバス
static M5Canvas offscreen(&M5.Display);
// カメラ映像用キャンバス
static M5Canvas image(&offscreen);
// メッセージ用キャンバス
static M5Canvas overlay(&offscreen);

//--------------------------------------------------------------------------------
/**
 * ESP32QRCodeReaderのカメラ初期化時の追加設定を行うためのコールバック
 */
static int on_camera_init(camera_config_t &config) {
  Serial.println("on_camera_init:");

  // どのBSPからかわからないけど少なくともM5StackCoreS3だとこれを呼び出さないと
  // #esp_camera_initがI2Cへアクセスできずカメラを初期化できない
  M5.In_I2C.release();

#ifdef USE_ATOMS3R_CAM
  #define POWER_GPIO_NUM 18
  pinMode(POWER_GPIO_NUM, OUTPUT);
  digitalWrite(POWER_GPIO_NUM, LOW);
  delay(500);
#endif
  config.frame_size = FRAMESIZE_VGA;

  return 0;
}


/**
 * ESP32QRCodeReaderからのフレーム毎のコールバック関数
 */
static int on_frame(camera_fb_t *fb) {
#if 0
	// ディスプレーサイズとカメラ映像サイズが同じとき
	image.pushGrayscaleImage(
		0, 0,
		fb->width, fb->height, // display_width, display_height,
		(const uint8_t *)fb->buf,
		lgfx::grayscale_8bit, WHITE, BLACK);
#else
	// ディスプレーサイズとカメラ映像サイズが違う時にディスプレーにフィットさせて表示
	image.pushGrayscaleImageRotateZoom(
		0.0f, 0.0f, // float dst_x, float dst_y,
		0.0f, 0.0f, // float src_x, float src_y,
		0.0f, 			// float angle,
		display_width / (float)fb->width, display_height / (float)fb->height, // float zoom_x, float zoom_y,
		fb->width, fb->height, // int32_t w, int32_t h,
		(const uint8_t *)fb->buf, // const uint8_t* image,
		lgfx::grayscale_8bit, WHITE, BLACK); // color_depth_t depth, const T& forecolor, const T& backcolor)
	#endif
	return 0;
}

//--------------------------------------------------------------------------------
/**
 * QRコード受信タスク
 * @param pvParameters
 */
void onQrCodeTask(void *pvParameters) {
  qr_code_data_t qrCodeData;

  while (true) {
    if (reader.receiveQrCode(qrCodeData, 100)) {
      Serial.println("Scanned new QRCode");
      if (qrCodeData.valid) {
        Serial.print("Valid payload: ");
        Serial.println((const char *)qrCodeData.payload);
        auto payload = String((const char *)qrCodeData.payload);
        if (last_payload != payload) {
          // 新しいペイロードを認識したときは音を鳴らす, 2.4Khzで80ms
          M5.Speaker.tone(2400, 80, 1);
          overlay.clear(); overlay.setCursor(0, 0);
          overlay.println(payload);
          last_payload = payload;
        }
      }
      else {
        Serial.print("Invalid payload: ");
        Serial.println((const char *)qrCodeData.payload);
        last_payload = "";
      }
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

//--------------------------------------------------------------------------------
void setup() {
  auto cfg = M5.config();
  CoreS3.begin(cfg);
  auto spk_cfg = M5.Speaker.config();
  M5.Speaker.config(spk_cfg);
  M5.Speaker.begin();
	delay(10);

	// デバッグ用にuartを初期化
  Serial.begin(115200);
  Serial.setDebugOutput(true);

	// ディスプレーを初期化
	M5.Display.setTextColor(CYAN);						// 文字色
	M5.Display.setFont(&fonts::lgfxJapanGothicP_20);	// フォント
	M5.Display.setTextSize(1);							// テキストサイズ
	M5.Display.fillScreen(BLACK);						// 背景設定
	M5.Display.setCursor(0, 0);							// 表示開始位置左上角（X,Y）
	M5.Display.setBrightness(32);						// 画面明るさ
  display_width = M5.Display.width();
  display_height = M5.Display.height();

	// オフスクリーン用キャンバスを生成
	offscreen.createSprite(display_width, display_height);
	// カメラ映像用キャンバスを生成
	image.createSprite(display_width, display_height);
	// オーバーレイ表示用キャンバスを生成
	overlay.createSprite(display_width, display_height);
	overlay.setTextColor(CYAN);							// 文字色
	overlay.setFont(&fonts::lgfxJapanGothicP_20);		// フォント
	overlay.setTextSize(1);								// テキストサイズ
	overlay.fillScreen(BLACK);						// 背景設定
	overlay.setCursor(0, 0);							// 表示開始位置左上角（X,Y）

  Serial.println();

  Serial.println("Setup QRCode Reader");
  reader.setup();
  auto sensor = esp_camera_sensor_get();
  if (sensor)
  {
    Serial.println("set sensor config");
    sensor->set_denoise(sensor, 1);
    sensor->set_hmirror(sensor, 0);
  } else {
    Serial.println("esp camera sensor not ready");
  }

  reader.beginOnCore(1);
  Serial.println("Begin on Core 1");

  reader.setDebug(false);

  xTaskCreateUniversal(onQrCodeTask, "onQrCode", 4 * 1024, nullptr, 4, nullptr, APP_CPU_NUM);
}

void loop() {
  M5.update();
  // オフスクリーンで映像とオーバーレイを合成
  image.pushSprite(0, 0);
  overlay.pushSprite(0, 0, BLACK);
  // オフスクリーンを画面へ転送
  offscreen.pushSprite(0, 0);
  delay(100);
}
