#ifndef ESP32_QR_CODE_ARDUINO_H_
#define ESP32_QR_CODE_ARDUINO_H_

#include "Arduino.h"
#include "ESP32CameraPins.h"
#include "esp_camera.h"
#include "quirc/quirc.h"

#ifndef QR_CODE_READER_STACK_SIZE
#define QR_CODE_READER_STACK_SIZE (40 * 1024)
#endif

#ifndef QR_CODE_READER_TASK_PRIORITY
#define QR_CODE_READER_TASK_PRIORITY (5)
#endif

/**
 * callback function to setup extra camera configs before calling #esp_camera_init
 */
typedef int (*on_camera_init_t)(camera_config_t &config);
/**
 * callback function for every camera frame.
 * should return as fast as possible and should not call #esp_camera_fb_return.
 */
typedef int (*on_frame_t)(camera_fb_t *fb);

enum QRCodeReaderSetupErr
{
  SETUP_OK,
  SETUP_NO_PSRAM_ERROR,
  SETUP_CAMERA_INIT_ERROR,
};

/* This structure holds the decoded QR-code data */
typedef struct _QRCodeData
{
  bool valid;
  int dataType;
  uint8_t payload[1024];
  int payloadLen;
} qr_code_data_t;

class QRCodeRecognizer
{
private:
  QueueHandle_t qrCodeQueue;
  struct quirc *quirc_image;
  uint16_t width;
  uint16_t height;
  size_t frame_bytes;
public:
  /**
   * constructor
   * @param max_queue_num max number of queued data
   */
  QRCodeRecognizer(const size_t &max_queue_num = 5);
  /**
   * destructor
   */
  ~QRCodeRecognizer();

  /**
   * set camera image(support 8 bits grayscale only) for QR code recognition
   * @param width
   * @param height
   * @param buf image data, 8 bits grayscale
   */
  bool prepare(const uint16_t &width, const uint16_t &height, const uint8_t *buf);
  /**
   * execute QR code recognition, the result are put into queue
   */
  bool parse();
  /**
   * get recognition result from queue
   * @param data
   * @param timuout_ms max wait time in millis if queue is empty
   */
  bool receive(qr_code_data_t &data, const long &timeout_ms);
};

class ESP32QRCodeReader
{
private:
  QRCodeRecognizer recognizer;
  TaskHandle_t qrCodeTaskHandler;
  CameraPins pins;
  framesize_t frame_size;
  camera_config_t cameraConfig;
  on_frame_t on_frame_cb;
  bool begun = false;
  bool debug = false;
  on_camera_init_t on_camera_init_cb;

  static void qrCodeDetectTask(void *taskData);
  void qrCodeDetectTaskFunc();
public:
  // Constructor
  ESP32QRCodeReader();
  ESP32QRCodeReader(const CameraPins &pins);
  ESP32QRCodeReader(const CameraPins &pins, const framesize_t &frame_size);
  ESP32QRCodeReader(const framesize_t &frame_size);
  ESP32QRCodeReader(
    const CameraPins &pins, const framesize_t &frame_size,
    on_camera_init_t init_cb, on_frame_t frame_cb
  );
  ~ESP32QRCodeReader();

  /**
   * Setup camera
   */
  QRCodeReaderSetupErr setup();

  /**
   * start recognition task on default core
   */
  void begin();
  /**
   * start recognition task on specified core
   * @param core
   */
  void beginOnCore(const BaseType_t &core = APP_CPU_NUM);
  /**
   * get recognition result from queue
   * @param data
   * @param timuout_ms max wait time in millis if queue is empty
   */
  bool receiveQrCode(qr_code_data_t &data, const long &timeout_ms);
  /**
   * end recognition task
   */
  void end();

  void setDebug(const bool &);
};

#endif // ESP32_QR_CODE_ARDUINO_H_
