#ifndef ESP32_QR_CODE_ARDUINO_H_
#define ESP32_QR_CODE_ARDUINO_H_

#include "Arduino.h"
#include "ESP32CameraPins.h"
#include "esp_camera.h"

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
struct QRCodeData
{
  bool valid;
  int dataType;
  uint8_t payload[1024];
  int payloadLen;
};

class ESP32QRCodeReader
{
private:
  TaskHandle_t qrCodeTaskHandler;
  CameraPins pins;
  framesize_t frameSize;
  on_camera_init_t on_camera_init_cb;
public:
  camera_config_t cameraConfig;
  QueueHandle_t qrCodeQueue;
  on_frame_t on_frame_cb;
  bool begun = false;
  bool debug = false;

  // Constructor
  ESP32QRCodeReader();
  ESP32QRCodeReader(const CameraPins &pins);
  ESP32QRCodeReader(const CameraPins &pins, const framesize_t &frameSize);
  ESP32QRCodeReader(const framesize_t &frameSize);
  ESP32QRCodeReader(
    const CameraPins &pins, const framesize_t &frameSize,
    on_camera_init_t init_cb, on_frame_t frame_cb
  );
  ~ESP32QRCodeReader();

  // Setup camera
  QRCodeReaderSetupErr setup();

  void begin();
  void beginOnCore(const BaseType_t &core);
  bool receiveQrCode(struct QRCodeData *qrCodeData, const long &timeoutMs);
  void end();

  void setDebug(const bool &);
};

#endif // ESP32_QR_CODE_ARDUINO_H_
