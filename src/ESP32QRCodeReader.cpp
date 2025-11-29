#include "ESP32QRCodeReader.h"

#include "quirc/quirc.h"
#include "Arduino.h"

static const char *TAG = "ESP32QRCode";

static void dumpData(const struct quirc_data *data)
{
  ESP_LOGV(TAG, "Version: %d", data->version);
  ESP_LOGV(TAG, "ECC level: %c", "MLHQ"[data->ecc_level]);
  ESP_LOGV(TAG, "Mask: %d", data->mask);
  ESP_LOGV(TAG, "Length: %d", data->payload_len);
  ESP_LOGV(TAG, "Payload: %s", data->payload);
}

//================================================================================
//
//================================================================================
/**
 * constructor
 */
/*public*/
QRCodeRecognizer::QRCodeRecognizer(const size_t &max_queue_num)
: qrCodeQueue(xQueueCreate(max_queue_num, sizeof(qr_code_data_t))),
  quirc_image(nullptr),
  width(0), height(0)
{
}

/**
 * destructor
 */
/*public*/
QRCodeRecognizer::~QRCodeRecognizer()
{
  auto q = quirc_image;
  quirc_image = nullptr;
  if (q)
  {
    quirc_destroy(q);
  }
  auto queue = qrCodeQueue;
  qrCodeQueue = nullptr;
  if (queue)
  {
    vQueueDelete(queue);
  }
}

/**
 * set camera image(support 8 bits grayscale only) for QR code recognition
 * @param width
 * @param height
 * @param image data, 8 bits grayscale
 */
/*public*/
bool QRCodeRecognizer::prepare(const uint16_t &width, const uint16_t &height, const uint8_t *buf)
{
  if (!quirc_image)
  {
    quirc_image = quirc_new();
  }

  if (quirc_image)
  {
    if (this->width != width || this->height != height || !frame_bytes)
    {
      ESP_LOGD(TAG, "Recognizer size change w h len: %d, %d", width, height);
      ESP_LOGD(TAG, "Resize the QR-code recognizer.");
      // Resize the QR-code recognizer.
      if (quirc_resize(quirc_image, width, height) < 0)
      {
        ESP_LOGE(TAG, "Resize the QR-code recognizer err (cannot allocate memory).");
        return false;
      }
      this->width = width;
      this->height = height;
      frame_bytes = width * height;
    }

    ESP_LOGV(TAG, "quirc_begin");

    const auto image = quirc_begin(quirc_image, nullptr, nullptr);
    ESP_LOGV(TAG, "Frame w h: %d, %d", width, height);
    memcpy(image, buf, frame_bytes);
    quirc_end(quirc_image);

    ESP_LOGV(TAG, "quirc_end");

    return true;
  } // if (quirc_image)

  return false;
}

/**
 * execute QR code recognition, the result are put into queue
 */
/*public*/
bool QRCodeRecognizer::parse()
{
  if (width && height && quirc_image)
  {
    const auto count = quirc_count(quirc_image);
    if (count == 0)
    {
      ESP_LOGD(TAG, "Error: not a valid qrcode");
      return false;
    }

    for (int i = 0; i < count; i++)
    {
      struct quirc_code code;
      struct quirc_data data;

      quirc_extract(quirc_image, i, &code);
      const auto err = quirc_decode(&code, &data);

      qr_code_data_t qrCodeData;

      if (err)
      {
        const auto error = quirc_strerror(err);
        const auto len = strlen(error);
        ESP_LOGD(TAG, "Decoding FAILED: %s", error);
        for (int i = 0; i < len; i++)
        {
          qrCodeData.payload[i] = error[i];
        }
        qrCodeData.valid = false;
        qrCodeData.payload[len] = '\0';
        qrCodeData.payloadLen = len;
      } else {
        ESP_LOGD(TAG, "Decoding successful:");
        dumpData(&data);

        qrCodeData.dataType = data.data_type;
        for (int i = 0; i < data.payload_len; i++)
        {
          qrCodeData.payload[i] = data.payload[i];
        }
        qrCodeData.valid = true;
        qrCodeData.payload[data.payload_len] = '\0';
        qrCodeData.payloadLen = data.payload_len;
      }
      xQueueSend(qrCodeQueue, &qrCodeData, (TickType_t)0);
      return qrCodeData.valid;
    } // for (int i = 0; i < count; i++)
  }

  return false;
}

/**
 * get recognition result from queue
 * @param data
 * @param timuout_ms max wait time in millis if queue is empty
 */
/*public*/
bool QRCodeRecognizer::receive(qr_code_data_t &data, const long &timeout_ms)
{
  if (qrCodeQueue)
  {
    return xQueueReceive(qrCodeQueue, &data, (TickType_t)pdMS_TO_TICKS(timeout_ms)) != 0;
  } else {
    return false;
  }
}

//================================================================================
//
//================================================================================
/*public*/
ESP32QRCodeReader::ESP32QRCodeReader()
: ESP32QRCodeReader(CAMERA_MODEL_AI_THINKER, FRAMESIZE_QVGA, nullptr, nullptr)
{
}

/*public*/
ESP32QRCodeReader::ESP32QRCodeReader(const framesize_t &frame_size)
: ESP32QRCodeReader(CAMERA_MODEL_AI_THINKER, frame_size, nullptr, nullptr)
{
}

/*public*/
ESP32QRCodeReader::ESP32QRCodeReader(const CameraPins &pins)
: ESP32QRCodeReader(pins, FRAMESIZE_QVGA, nullptr, nullptr)
{
}

/*public*/
ESP32QRCodeReader::ESP32QRCodeReader(const CameraPins &pins, const framesize_t &frame_size)
: ESP32QRCodeReader(pins, frame_size, nullptr, nullptr)
{
}

/*public*/
ESP32QRCodeReader::ESP32QRCodeReader(
  const CameraPins &pins, const framesize_t &frame_size,
  on_camera_init_t init_cb, on_frame_t frame_cb
)
: recognizer(QRCodeRecognizer(10)),
  pins(pins), frame_size(frame_size),
  on_camera_init_cb(init_cb), on_frame_cb(frame_cb)
{
}

/*public*/
ESP32QRCodeReader::~ESP32QRCodeReader()
{
  end();
}

/**
 * Setup camera
 */
/*public*/
QRCodeReaderSetupErr ESP32QRCodeReader::setup()
{
  if (!psramFound())
  {
    return SETUP_NO_PSRAM_ERROR;
  }

  cameraConfig.ledc_channel = LEDC_CHANNEL_0;
  cameraConfig.ledc_timer = LEDC_TIMER_0;
  cameraConfig.pin_d0 = pins.Y2_GPIO_NUM;
  cameraConfig.pin_d1 = pins.Y3_GPIO_NUM;
  cameraConfig.pin_d2 = pins.Y4_GPIO_NUM;
  cameraConfig.pin_d3 = pins.Y5_GPIO_NUM;
  cameraConfig.pin_d4 = pins.Y6_GPIO_NUM;
  cameraConfig.pin_d5 = pins.Y7_GPIO_NUM;
  cameraConfig.pin_d6 = pins.Y8_GPIO_NUM;
  cameraConfig.pin_d7 = pins.Y9_GPIO_NUM;
  cameraConfig.pin_xclk = pins.XCLK_GPIO_NUM;
  cameraConfig.pin_pclk = pins.PCLK_GPIO_NUM;
  cameraConfig.pin_vsync = pins.VSYNC_GPIO_NUM;
  cameraConfig.pin_href = pins.HREF_GPIO_NUM;
  cameraConfig.pin_sscb_sda = pins.SIOD_GPIO_NUM;
  cameraConfig.pin_sscb_scl = pins.SIOC_GPIO_NUM;
  cameraConfig.pin_pwdn = pins.PWDN_GPIO_NUM;
  cameraConfig.pin_reset = pins.RESET_GPIO_NUM;
  cameraConfig.xclk_freq_hz = 10000000;
  cameraConfig.pixel_format = PIXFORMAT_GRAYSCALE;

  //cameraConfig.frame_size = FRAMESIZE_VGA;
  cameraConfig.frame_size = frame_size;
  cameraConfig.jpeg_quality = 15;
  cameraConfig.fb_count = 1;

  if (on_camera_init_cb)
  {
    on_camera_init_cb(cameraConfig);
  }
  // camera init
  esp_err_t err = esp_camera_init(&cameraConfig);
  if (err != ESP_OK)
  {
    return SETUP_CAMERA_INIT_ERROR;
  }
  return SETUP_OK;
}

/*private,static*/
void ESP32QRCodeReader::qrCodeDetectTask(void *taskData) {
  auto self = (ESP32QRCodeReader *)taskData;
  if (self) {
    self->qrCodeDetectTaskFunc();
  }
}

/*private*/
void ESP32QRCodeReader::qrCodeDetectTaskFunc()
{
  camera_config_t camera_config = cameraConfig;
  if (camera_config.frame_size > FRAMESIZE_SVGA)
  {
    if (debug)
    {
      Serial.println("Camera Size err");
    }
    vTaskDelete(nullptr);
    return;
  }

  while (true)
  {
    if (debug)
    {
      ESP_LOGD(TAG, "alloc qr heap: %u", xPortGetFreeHeapSize());
      ESP_LOGD(TAG, "uxHighWaterMark = %d", uxTaskGetStackHighWaterMark(nullptr));
      ESP_LOGD(TAG, "begin camera get fb");
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);

    const auto fb = esp_camera_fb_get();
    if (!fb)
    {
      if (debug)
      {
        Serial.println("Camera capture failed");
      }
      continue;
    }

    recognizer.prepare(fb->width, fb->height, fb->buf);
    if (on_frame_cb)
    {
      on_frame_cb(fb);
    }
    esp_camera_fb_return(fb);

    recognizer.parse();
  } // while (true)

  if (debug)
  {
    Serial.println("decode task finsihed");
  }

  vTaskDelete(nullptr);
}

/**
 * start recognition task on default core
 */
/*public*/
void ESP32QRCodeReader::begin()
{
  beginOnCore(APP_CPU_NUM);
}

/**
 * start recognition task on specified core
 * @param core
 */
/*public*/
void ESP32QRCodeReader::beginOnCore(const BaseType_t &core)
{
  if (!begun)
  {
    xTaskCreateUniversal(qrCodeDetectTask, "qrCodeDetectTask", QR_CODE_READER_STACK_SIZE, this, QR_CODE_READER_TASK_PRIORITY, &qrCodeTaskHandler, core);
    begun = true;
  }
}

/**
 * get recognition result from queue
 * @param data
 * @param timuout_ms max wait time in millis if queue is empty
 */
/*public*/
bool ESP32QRCodeReader::receiveQrCode(qr_code_data_t &data, const long &timeout_ms)
{
  return recognizer.receive(data, timeout_ms);
}

/**
 * end recognition task
 */
/*public*/
void ESP32QRCodeReader::end()
{
  if (begun)
  {
    TaskHandle_t tmpTask = qrCodeTaskHandler;
    if (qrCodeTaskHandler != NULL)
    {
      qrCodeTaskHandler = NULL;
      vTaskDelete(tmpTask);
    }
  }
  begun = false;
}

/*public*/
void ESP32QRCodeReader::setDebug(const bool &on)
{
  debug = on;
}
