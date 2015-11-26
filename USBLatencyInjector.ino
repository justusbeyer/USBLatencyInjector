// Emulation of USB keyboard and mouse
#include <Keyboard.h>
#include <Mouse.h>

#include <hidboot.h>
#include <usbhub.h>

// Satisfy IDE, which only needs to see the include statment in the ino.
#ifdef dobogusinclude
#include <spi4teensy3.h>
#include <SPI.h>
#endif

#include <HID.h>

#define KAPUTT 1

const unsigned long DELAY_MILLIS = 600;

const int REPORT_QUEUE_LENGTH = 82;
const int MAX_REPORT_LENGTH = 8;

class ReportDelayRelay
{
public:
  ReportDelayRelay() {
    nextReportIndex = 0;
    oldestReportIndex = 0;
    reportsWaiting = 0;
  }

  void enqueueReport(uint8_t hid_id, const uint8_t* data, int len) {
    if (reportsWaiting >= REPORT_QUEUE_LENGTH) {
      Serial.println("Queue overflow.");
      return;
    }

    if (len > 8) {
      Serial.println("Report too long");
      return;
    }

    // Copy report
    reports[nextReportIndex].enqueueMillis = millis();
    reports[nextReportIndex].hid_id = hid_id;
    memset(reports[nextReportIndex].bytes, 0x00, MAX_REPORT_LENGTH); // Zero that report buffer
    memcpy(reports[nextReportIndex].bytes, data, len);

    // Hack for mouse messages without scroll wheel info: We add the scroll wheel byte
    if (hid_id == 1 && len <= 3)
      reports[nextReportIndex].bytes_length = 4;
    else
      reports[nextReportIndex].bytes_length = len;

    // Log
#if KAPUTT
    Serial.print("+");
    Serial.print("(");
    Serial.print(nextReportIndex);
    Serial.print(", ");
    Serial.print(hid_id);
    Serial.print(", ");
    for (int i=0; i<len; i++) { Serial.print(data[i], HEX); Serial.print(":"); }
    Serial.print(", ");
    Serial.print(len);
    Serial.print(", ");
    Serial.print(reports[nextReportIndex].enqueueMillis);
    Serial.println(")");
#endif

    // Enqueue report
    nextReportIndex = (nextReportIndex + 1) % REPORT_QUEUE_LENGTH;
    reportsWaiting++;
  }

  // Forward pending reports.
  void tick() {
    // Check if something is to be forwarded
    if(reportsWaiting > 0 &&
       (reports[oldestReportIndex].enqueueMillis + DELAY_MILLIS) <= millis())
    {
      // Forward report
      HID().SendReport(
        reports[oldestReportIndex].hid_id,
        reports[oldestReportIndex].bytes,
        reports[oldestReportIndex].bytes_length);

      // Log
#if KAPUTT
      Serial.print("-");
      Serial.print("(");
      Serial.print(oldestReportIndex);
      Serial.print(", ");
      Serial.print(reports[oldestReportIndex].hid_id);
      Serial.print(", ");
      for (int i=0; i<reports[oldestReportIndex].bytes_length; i++) { Serial.print(reports[oldestReportIndex].bytes[i], HEX); Serial.print(":"); }
      Serial.print(", ");
      Serial.print(reports[oldestReportIndex].bytes_length);
      Serial.println(")");
#endif

      // Mark report as sent
      reportsWaiting--;
      oldestReportIndex = (oldestReportIndex + 1) % REPORT_QUEUE_LENGTH;
    }
  }

private:
  struct SReport {
     unsigned long enqueueMillis;
     uint8_t hid_id;
     uint8_t bytes[MAX_REPORT_LENGTH]; // Keyboard reports are 8 bytes, mouse 4 bytes, 8 bytes should be safe
     uint8_t bytes_length;
  };

  SReport reports[REPORT_QUEUE_LENGTH]; // Store up to 8 reports.
  uint8_t nextReportIndex; // points to next free array element in _reports
  uint8_t oldestReportIndex; // points to oldest unsent report
  uint8_t reportsWaiting;
};
ReportDelayRelay reportDelayRelay;

class MouseRptParser : public MouseReportParser
{
  void Parse(usb_host_shield::HID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) {
    reportDelayRelay.enqueueReport(1, buf, len);
    /*HID().SendReport(
        1,
        buf,
        len);*/

    // Mouse.move(100,100, 0);

    
  }
};

class KbdRptParser : public KeyboardReportParser
{
    void Parse(usb_host_shield::HID *hid, bool is_rpt_id, uint8_t len, uint8_t *buf) {
      reportDelayRelay.enqueueReport(2, buf, len);
      /*HID().SendReport(
        2,
        buf,
        len);
      Serial.print("K recv len ");
      Serial.println(len);*/
    }
};

USB     Usb;
USBHub     Hub(&Usb);

HIDBoot < HID_PROTOCOL_KEYBOARD | HID_PROTOCOL_MOUSE > HidComposite(&Usb);
HIDBoot<HID_PROTOCOL_KEYBOARD>    HidKeyboard(&Usb);
HIDBoot<HID_PROTOCOL_MOUSE>    HidMouse(&Usb);

//uint32_t next_time;

KbdRptParser KbdPrs;
MouseRptParser MousePrs;

void setup()
{
  // Begin emulating an USB keyboard and mouse
  Keyboard.begin();
  Mouse.begin();
  
  Serial.begin( 115200 );
#if !defined(__MIPSEL__)
  while (!Serial); // Wait for serial port to connect - used on Leonardo, Teensy and other boards with built-in USB CDC serial connection
#endif
  Serial.println("Start");

  if (Usb.Init() == -1)
    Serial.println("OSC did not start.");

  delay( 200 );

  //next_time = millis() + 5000;

  HidComposite.SetReportParser(0, (HIDReportParser*)&KbdPrs);
  HidComposite.SetReportParser(1, (HIDReportParser*)&MousePrs);
  HidKeyboard.SetReportParser(0, (HIDReportParser*)&KbdPrs);
  HidMouse.SetReportParser(0, (HIDReportParser*)&MousePrs);
}

void loop()
{
  Usb.Task();
  reportDelayRelay.tick();
}

