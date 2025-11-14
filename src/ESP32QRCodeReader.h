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

public:
  camera_config_t cameraConfig;
  QueueHandle_t qrCodeQueue;
  bool begun = false;
  bool debug = false;

  // Constructor
  ESP32QRCodeReader();
  ESP32QRCodeReader(const CameraPins &pins);
  ESP32QRCodeReader(const CameraPins &pins, const framesize_t &frameSize);
  ESP32QRCodeReader(const framesize_t &frameSize);
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
