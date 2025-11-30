/*
 * QRCodeReader sample sketch for M5CoreS3
 * Copyright 2025 t_saki@serenegiant.com
 */

#include <Arduino.h>
#include <M5CoreS3.h>
#include <esp_camera.h>
#include <ESP32QRCodeReader.h>

#define CAMERA_MODEL_M5STACK_CORES3          \
	{                                        \
		.pin_pwdn = -1,                      \
		.pin_reset = -1,                     \
		.pin_xclk = -1,                      \
		.pin_sccb_sda = 12,                  \
		.pin_sccb_scl = 11,                  \
		.pin_d7 = 47,                        \
		.pin_d6 = 48,                        \
		.pin_d5 = 16,                        \
		.pin_d4 = 15,                        \
		.pin_d3 = 42,                        \
		.pin_d2 = 41,                        \
		.pin_d1 = 40,                        \
		.pin_d0 = 39,                        \
		.pin_vsync = 46,                     \
		.pin_href = 38,                      \
		.pin_pclk = 45,                      \
                                             \
		.xclk_freq_hz = 10000000,            \
		.ledc_timer = LEDC_TIMER_0,          \
		.ledc_channel = LEDC_CHANNEL_0,      \
		.pixel_format = PIXFORMAT_GRAYSCALE, \
		.frame_size = FRAMESIZE_QVGA,        \
		.jpeg_quality = 0,                   \
		.fb_count = 2,                       \
		.fb_location = CAMERA_FB_IN_PSRAM,   \
		.grab_mode = CAMERA_GRAB_WHEN_EMPTY}

//--------------------------------------------------------------------------------
// 変数宣言
//--------------------------------------------------------------------------------
static QRCodeRecognizer recognizer;
TaskHandle_t hCameraTask;
static TaskHandle_t hReaderTask;
static int display_width = 0;
static int display_height = 0;
// オフスクリーン描画用のキャンバス
static M5Canvas offscreen(&M5.Display);
// カメラ映像用キャンバス
static M5Canvas image(&offscreen);
// メッセージ用キャンバス
static M5Canvas overlay(&offscreen);

static void readerTask(void *)
{
	qr_code_data_t data;
	String last_payload = "";

	while (true)
	{
		if (recognizer.receive(data, 100))
		{
			Serial.println("Scanned new QRCode");
			if (data.valid)
			{
				Serial.print("Valid payload: ");
				Serial.println((const char *)data.payload);
				auto payload = String((const char *)data.payload);
				if (last_payload != payload)
				{	// 新しいペイロードを認識したとき
					last_payload = payload;
					M5.Speaker.tone(2400, 80, 1);
					overlay.clear();
					overlay.setCursor(0, 0);
					overlay.println(payload);
				}
			} else {
				Serial.print("Invalid payload: ");
				Serial.println((const char *)data.payload);
				last_payload = "";
			}
		}
		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

/**
 * カメラ映像の取得＆QRコード検出＆デコード処理用タスク
 * loop関数だとスタックが足りないので専用タスクとして
 * 実行する
 */
static void cameraTask(void *)
{
	while (true)
	{
		vTaskDelay(100 / portTICK_PERIOD_MS);
		const auto fb = esp_camera_fb_get();
		if (fb)
		{
#if 1
			// ディスプレーサイズとカメラ映像サイズが同じとき
			image.pushGrayscaleImage(
				0, 0,
				fb->width, fb->height, // display_width, display_height,
				(const uint8_t *)fb->buf,
				lgfx::grayscale_8bit, WHITE, BLACK);
#else
			// ディスプレーサイズとカメラ映像サイズが違う時にディスプレーにフィットさせて表示
			image.pushGrayscaleImageRotateZoom(
				0.0f, 0.0f,															  // float dst_x, float dst_y,
				0.0f, 0.0f,															  // float src_x, float src_y,
				0.0f,																  // float angle,
				display_width / (float)fb->width, display_height / (float)fb->height, // float zoom_x, float zoom_y,
				fb->width, fb->height,												  // int32_t w, int32_t h,
				(const uint8_t *)fb->buf,											  // const uint8_t* image,
				lgfx::grayscale_8bit, WHITE, BLACK);								  // color_depth_t depth, const T& forecolor, const T& backcolor)
#endif
			// QRコード処理用に映像をセット
			recognizer.prepare(fb->width, fb->height, fb->buf);
			// 取得したフレームを解放
			esp_camera_fb_return(fb);
			// QRコード検出＆デコード処理
			recognizer.parse();
		} else {
			Serial.println("failed to get image from camera");
		}
	}
}

//--------------------------------------------------------------------------------
void setup()
{
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
	M5.Display.setTextColor(CYAN);					 // 文字色
	M5.Display.setFont(&fonts::lgfxJapanGothicP_20); // フォント
	M5.Display.setTextSize(1);						 // テキストサイズ
	M5.Display.fillScreen(BLACK);					 // 背景設定
	M5.Display.setCursor(0, 0);						 // 表示開始位置左上角（X,Y）
	M5.Display.setBrightness(32);					 // 画面明るさ
	display_width = M5.Display.width();
	display_height = M5.Display.height();

	// オフスクリーン用キャンバスを生成
	offscreen.createSprite(display_width, display_height);
	// カメラ映像用キャンバスを生成
	image.createSprite(display_width, display_height);
	// オーバーレイ表示用キャンバスを生成
	overlay.createSprite(display_width, display_height);
	overlay.setTextColor(CYAN);					  // 文字色
	overlay.setFont(&fonts::lgfxJapanGothicP_20); // フォント
	overlay.setTextSize(1);						  // テキストサイズ
	overlay.fillScreen(BLACK);					  // 背景設定
	overlay.setCursor(0, 0);					  // 表示開始位置左上角（X,Y）

	Serial.println("init camera");
	camera_config_t cameraConfig = CAMERA_MODEL_M5STACK_CORES3;
	// いつのBSPかわからないけどこれを呼び出さないとesp_camera_initからI2Cへ
	// アクセスできず初期化に失敗する
	M5.In_I2C.release();
	// CoreS3.Camera.begin後にCoreS3.Camera.sensor->set_pixformatで
	// グレースケールにしようとするとクラッシュするのでesp_camera_initを自前で
	// 呼び出す
	esp_err_t err = esp_camera_init(&cameraConfig);
	if (err == ESP_OK)
	{ // 初期化に成功した
		auto sensor = esp_camera_sensor_get();
		if (sensor)
		{
			Serial.println("set sensor config");
			sensor->set_denoise(sensor, 1);
			// M5CoreS3はそのままだと映像が左右反転するのでhmirrorを0にする
			sensor->set_hmirror(sensor, 0);
		} else {
			Serial.println("esp camera sensor not ready");
		}
		// 読み込みタスクはQVGA(320x240)VGA(640x480)で3KBぐらいスタックを使う
		xTaskCreateUniversal(readerTask, "ReaderTask", 4 * 1024, nullptr, 2, &hReaderTask, APP_CPU_NUM);
		// カメラタスクはQVGA(320x240)VGA(640x480)で15KBぐらいスタックを使う
		xTaskCreateUniversal(cameraTask, "CameraTask", 20 * 1024, nullptr, 4, &hCameraTask, APP_CPU_NUM);
	} else {
		Serial.println("Failed to init camera");
	}
}

/**
 * 定期的に呼ばれる処理
 * プログラム的ではここでカメラ映像の取得と解析をしてもいいんだけど
 * スタックが最大8KBしかなくてクラッシュするので専用タスクとして
 * 実行する
 */
void loop()
{
	M5.update();
	// オフスクリーンで映像とオーバーレイを合成
	image.pushSprite(0, 0);
	overlay.pushSprite(0, 0, BLACK);
	// オフスクリーンを画面へ転送
	offscreen.pushSprite(0, 0);
	delay(100);
}
