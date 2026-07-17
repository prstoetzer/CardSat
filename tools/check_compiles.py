#!/usr/bin/env python3
"""Compile gate: do usbserial.{h,cpp} and rotator.{h,cpp} COMPILE AND LINK?

Why this exists. fix30 shipped a build error to the bench: snapshotHeadroom()
called taskHeadroomByName() ~470 lines before its definition. Every existing gate
passed -- and they had to, because they were all asking the wrong question:

  check_balance  -- braces balance      (they did)
  check_parity   -- src == .ino         (they matched: BOTH were broken identically)
  check_idf_symbols -- symbols exist    (it did exist... further down the file)

Parity is the trap worth naming: it proves the two representations AGREE, which
is worthless when the shared content is wrong. Nothing was asking a compiler.

This does. It stubs the Arduino/IDF/EspUsbHost world (structure only -- no
behavior) and compiles the REAL src/usbserial.h + src/usbserial.cpp bodies with
the host g++, then LINKS (catching the anonymous-namespace linkage trap that a
syntax-only check misses). It cannot validate hardware behavior and does not try;
it answers one question -- "does this build?" -- which is exactly the question
that reached the bench unanswered.

Skips cleanly if g++ is absent. Run from the repo root.
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CPP  = os.path.join(ROOT, 'src', 'usbserial.cpp')
HDR  = os.path.join(ROOT, 'src', 'usbserial.h')

if subprocess.call(['which', 'g++'], stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL) != 0:
    print('  compile gate: SKIPPED (no g++ on this machine)')
    sys.exit(0)
if not (os.path.isfile(CPP) and os.path.isfile(HDR)):
    print('  compile gate: SKIPPED (usbserial sources not found)')
    sys.exit(0)

ROT_STUB = r'''
// --- rotator.cpp's world -------------------------------------------------------
struct TwoWire { void begin(int,int,uint32_t){} void beginTransmission(uint8_t){}
  size_t write(uint8_t){return 1;} uint8_t endTransmission(){return 0;}
  uint8_t requestFrom(int,int){return 1;} int available(){return 0;} int read(){return -1;} };
static TwoWire Wire1;
struct WiFiClient : Stream { int connect(const char*,uint16_t){return 1;} void stop(){}
  int connected(){return 1;} int read(){return -1;} int peek(){return -1;} void flush(){}
  size_t write(uint8_t){return 1;} };
struct WiFiUDP { int begin(uint16_t){return 1;} int beginPacket(const char*,uint16_t){return 1;}
  size_t write(const uint8_t*,size_t){return 1;} int endPacket(){return 1;}
  int parsePacket(){return 0;} int read(uint8_t*,size_t){return 0;} void stop(){} };
struct HardwareSerial : Stream { HardwareSerial(int){} void begin(unsigned long,uint32_t,int,int){}
  int available(){return 0;} int read(){return -1;} int peek(){return -1;} void flush(){}
  size_t write(uint8_t){return 1;} };
#define SERIAL_8N1 0x800001c
// The network rotator backends are stripped from the gate's TU (see rot_body),
// but makeRotator() still constructs them. Minimal stand-ins keep that function
// type-checking without dragging in the Arduino network stack. They inherit
// Rotator, which rotator.h declares just below, so these are injected AFTER it
// via the ROT_NET_STUB spliced in by the builder.
#include <cmath>       // lroundf/roundf used by the rotator maths
'''

STUB = r'''
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#define CARDSAT_HAS_USBCAT 1
typedef void* TaskHandle_t;
typedef int BaseType_t; typedef unsigned UBaseType_t;
#define configMAX_TASK_NAME_LEN 16
#define RTC_NOINIT_ATTR
struct Print {
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t i=0; for(;i<n;i++) write(b[i]); return i; }
  size_t write(const char* s){ size_t n=0; while(s&&*s){ write((uint8_t)*s++); n++; } return n; }
  size_t print(const char* s){ return write(s); }
  size_t print(char c){ return write((uint8_t)c); }
  size_t print(int){ return 0; }
  size_t print(unsigned){ return 0; }
  size_t print(unsigned long){ return 0; }
  size_t print(long){ return 0; }
  size_t print(double, int = 2){ return 0; }
  // println/printf exist on the real Arduino Print (Print.h:71, :90+) and the
  // ~181 Serial call sites use them, so the stub must have them too -- else the
  // gate rejects code that compiles perfectly on the device.
  size_t println(){ return write((uint8_t)'\n'); }
  size_t println(const char* s){ size_t n = print(s); return n + println(); }
  size_t printf(const char*, ...){ return 0; }
};
struct Stream : public Print {
  virtual int available() { return 0; }
  virtual int read()      { return -1; }
  virtual int peek()      { return -1; }
  virtual void flush()    {}
  size_t write(uint8_t) override { return 1; }
};
// Minimal Arduino String: enough to type-check the diagnostic accessors.
#include <string>
// Enough of Arduino String for the rotator/usbserial surfaces. Not a
// reimplementation -- just the methods these modules actually call.
struct String : std::string {
  String(){} String(const char* s):std::string(s?s:""){}
  String(int v):std::string(std::to_string(v)){}
  String(unsigned v):std::string(std::to_string(v)){}
  String(long v):std::string(std::to_string(v)){}
  String(float v, int=2):std::string(std::to_string(v)){}
  const char* c_str() const { return std::string::c_str(); }
  size_t length() const { return std::string::length(); }
  int indexOf(char c, int from=0) const { auto p=find(c,from); return p==npos?-1:(int)p; }
  int indexOf(const char* s, int from=0) const { auto p=find(s,from); return p==npos?-1:(int)p; }
  String substring(int a) const { return String(std::string::substr(a).c_str()); }
  String substring(int a,int b) const { return String(std::string::substr(a,b-a).c_str()); }
  float toFloat() const { return atof(c_str()); }
  int   toInt()   const { return atoi(c_str()); }
  void  trim() {}
  bool  startsWith(const char* s) const { return rfind(s,0)==0; }
  String& operator+=(const char* s){ std::string::operator+=(s); return *this; }
  String& operator+=(const String& s){ std::string::operator+=(s); return *this; }
  String& operator+=(char c){ std::string::operator+=(c); return *this; }
};
static inline String operator+(const String& a, const char* b){ String r=a; r+=b; return r; }
static inline String operator+(const String& a, const String& b){ String r=a; r+=b; return r; }
static uint32_t g_ms=0; static inline uint32_t millis(){ return g_ms+=10; }
static inline void delay(uint32_t){}
static inline TaskHandle_t xTaskGetHandle(const char*){ return nullptr; }
static inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t){ return 1000; }
static inline UBaseType_t uxTaskGetNumberOfTasks(){ return 10; }
struct SerialStub { operator bool(){return true;} void flush(){} void end(){} void begin(int){}
  int printf(const char*, ...){ return 0; }
  int println(const char*){ return 0; } int println(){ return 0; }
  int print(const char*){ return 0; } };
// config.h now defines a Print-derived Tee over HWCDCSerial and redefines the
// `Serial` macro to it (see consolelog.h). The gate must model the REAL name the
// core uses -- `#define Serial HWCDCSerial` (HardwareSerial.h:413) -- or config.h
// will not compile here even though it compiles on the device.
struct HWCDCStub : public Print {
  size_t write(uint8_t) override { return 1; }
  size_t write(const uint8_t*, size_t n) override { return n; }
  int available(){return 0;} int read(){return -1;} int peek(){return -1;}
  void flush(){} void begin(unsigned long){} void end(){}
  void setDebugOutput(bool){}
  int printf(const char*, ...){ return 0; }
  int println(const char*){ return 0; } int println(){ return 0; }
  int print(const char*){ return 0; }
  operator bool(){ return true; }
};
static HWCDCStub HWCDCSerial;
static SerialStub Serial;
struct ESPStub { uint32_t getFreeHeap(){return 80000;} }; static ESPStub ESP;
static inline size_t heap_caps_get_largest_free_block(int){ return 30000; }
#define MALLOC_CAP_8BIT 0
typedef int esp_err_t; typedef void* esp_task_wdt_user_handle_t;
#define ESP_OK 0
static inline esp_err_t esp_task_wdt_add_user(const char*, esp_task_wdt_user_handle_t*){return 0;}
static inline esp_err_t esp_task_wdt_reset_user(esp_task_wdt_user_handle_t){return 0;}
static inline esp_err_t esp_task_wdt_delete_user(esp_task_wdt_user_handle_t){return 0;}
static inline esp_err_t usb_host_lib_unblock(){return 0;}
// Signatures verified against IDF v5.4 components/usb/include/usb/usb_host.h and
// esp_common/include/esp_err.h -- the gate must model the REAL API, or it either
// misses errors or invents them.
#define ESP_ERR_INVALID_STATE 0x103
typedef unsigned TickType_t;
static inline esp_err_t usb_host_lib_handle_events(TickType_t, uint32_t* ev){ if(ev)*ev=0; return 0; }
static inline esp_err_t usb_host_uninstall(){return 0;}
static inline esp_err_t usb_host_device_free_all(){return 0;}
// usb_host.h:80-83 (fields verified) + :210 signature
typedef struct { int num_devices; int num_clients; } usb_host_lib_info_t;
static inline esp_err_t usb_host_lib_info(usb_host_lib_info_t* i){ if(i){i->num_devices=0;i->num_clients=0;} return 0; }
#define ESP_ERR_TIMEOUT 0x107
#define USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS 0x01
#define USB_HOST_LIB_EVENT_FLAGS_ALL_FREE   0x02
typedef void* esp_timer_handle_t;
struct esp_timer_create_args_t { void (*callback)(void*); int dispatch_method; const char* name; };
#define ESP_TIMER_TASK 0
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t*){return 0;}
static inline esp_err_t esp_timer_start_periodic(esp_timer_handle_t,uint64_t){return 0;}
static inline esp_err_t esp_timer_stop(esp_timer_handle_t){return 0;}
struct EspUsbHostSerialConfig { uint32_t baud; uint8_t dataBits; int parity; int stopBits; };
typedef int EspUsbHostSerialParity; typedef int EspUsbHostSerialStopBits;
struct EspUsbHostConfig { uint32_t taskStackSize=8192; UBaseType_t taskPriority=5; BaseType_t taskCore=0; };
struct EspUsbHostDeviceInfo {   // fields per EspUsbHost.h:169-183
  uint8_t address = 0; uint16_t vid = 0, pid = 0;
  const char* manufacturer = ""; const char* product = ""; const char* serial = "";
  uint8_t parentAddress = 0; uint8_t portId = 0;
  uint16_t usbVersion = 0, deviceVersion = 0;
  uint8_t deviceClass = 0, deviceSubClass = 0;
};
struct EspUsbHost {
  bool begin(const EspUsbHostConfig&){return true;} void end(){}
  int lastError(){return 0;}
  template<class F> void onDeviceConnected(F){}
};
struct EspUsbHostCdcSerial : Stream {
  EspUsbHostCdcSerial(EspUsbHost&){}
  bool begin(uint32_t){return true;} void end(){} bool connected(){return true;}
  void setConfig(const EspUsbHostSerialConfig&){} void setDtr(bool){} void setRts(bool){}
  void setAddress(uint8_t){}          // EspUsbHost.h:1695 -- the misbind guard
  int read(){return -1;} int peek(){return -1;} void flush(){}
  size_t write(uint8_t){return 1;}
};
#include <new>   // real std::nothrow (the String stub pulls in libstdc++ anyway)
'''

def body(path):
    s = open(path, encoding='utf-8').read()
    i = s.find('namespace UsbSerial {')
    if i < 0:
        print(f'  compile gate: SKIPPED (no UsbSerial namespace in {path})')
        sys.exit(0)
    j = s.rfind('#endif  // CARDSAT_HAS_USBCAT')
    return s[i:j if j > i else len(s)]

# rotator.{h,cpp} joins the gate because rotator.cpp now CALLS UsbSerial:: -- and
# the first version shipped without including usbserial.h, which a usbserial-only
# gate could never catch. Anything that crosses module boundaries belongs here.
CFG_H   = os.path.join(ROOT, 'src', 'config.h')
LOG_H   = os.path.join(ROOT, 'src', 'logstore.h')
LOG_CPP = os.path.join(ROOT, 'src', 'logstore.cpp')
SET_H   = os.path.join(ROOT, 'src', 'settings.h')   # RotType/RotTransport enums
ROT_H   = os.path.join(ROOT, 'src', 'rotator.h')
ROT_CPP = os.path.join(ROOT, 'src', 'rotator.cpp')

def rot_body(path, drop_classes=()):
    """rotator source with #includes stripped, optionally dropping whole classes.

    The network backends (RotctlRotator / PstRotator) are EXCLUDED on purpose.
    They need a WiFi/WiFiClient/WiFiUdp surface to type-check, and stubbing all
    of that would mean modelling most of the Arduino network stack -- a large,
    drifting fake that guards code this change never touches. The gate covers
    what the refactor actually altered: the serial transports (BridgeStream,
    UsbRotStream) and the three serial protocols that now run over a Stream.
    """
    s = open(path, encoding='utf-8').read()
    for cls in drop_classes:
        # drop `class X ... };` in the header
        a = s.find('class ' + cls)
        while a >= 0:
            b = s.find('\n};', a)
            if b < 0: break
            s = s[:a] + s[b+3:]
            a = s.find('class ' + cls)
        # drop `RetType X::method(...) { ... }` blocks in the .cpp
        while True:
            i = s.find(cls + '::')
            if i < 0: break
            ls = s.rfind('\n', 0, i) + 1
            depth = 0; j = s.find('{', i); 
            if j < 0: break
            k = j
            while k < len(s):
                if s[k] == '{': depth += 1
                elif s[k] == '}':
                    depth -= 1
                    if depth == 0: break
                k += 1
            s = s[:ls] + s[k+1:]
    out = []
    for line in s.split('\n'):
        if line.startswith('#include') or line.startswith('#pragma once'):
            continue
        out.append(line)
    return '\n'.join(out)

tu = STUB + body(HDR) + '\n' + body(CPP) + '\n'
if os.path.isfile(ROT_H) and os.path.isfile(ROT_CPP):
    # The real config.h constants (ROT_I2C_*, ROT_XTAL_HZ, ...) rather than copies:
    # copies drift. Only the constexpr/# defines are needed; strip its includes.
    cfg_txt = rot_body(CFG_H) if os.path.isfile(CFG_H) else ''
    # settings.h carries the RotType/RotTransport enums makeRotator switches on.
    # Real header, not copies: an enum value that drifts is exactly the bug class
    # this gate exists to catch (see the ROT_PST clamp that ate Easycomm configs).
    # Only the ROT_* enums, not the whole Settings struct: that struct pulls in
    # radio_profiles.h and the entire app config surface, which is outside this
    # gate's job. Extract the two enum blocks verbatim so the VALUES stay real.
    set_txt = ''
    if os.path.isfile(SET_H):
        st = open(SET_H, encoding='utf-8').read()
        for tag in ('enum RotType : uint8_t {', 'enum RotTransport : uint8_t {'):
            a = st.find(tag)
            if a >= 0:
                b = st.find('};', a)
                set_txt += st[a:b+2] + '\n'
        a = st.find('static constexpr uint8_t ROT_XPORT_N')
        if a >= 0:
            set_txt += st[a:st.find('\n', a)] + '\n'
    cfg_txt = cfg_txt + '\n' + set_txt
    # logstore: new code, real failure modes (varargs, rotation, path handling).
    # Needs a tiny fs::FS/File surface -- far smaller than the network stack, so
    # unlike the rotator's WiFi backends this one is worth modelling.
    LOG_STUB = """
#include <stdarg.h>     // logstore.cpp's va_list (rot_body strips its #includes)
namespace fs { struct FS; }
struct File {
  operator bool() const { return true; }
  size_t size() const { return 0; }
  int print(const char*){ return 0; } int print(char){ return 0; }
  int printf(const char*, ...){ return 0; }
  void flush(){} void close(){}
};
namespace fs {
  struct FS {
    File open(const char*, const char*){ return File(); }
    bool exists(const char*){ return false; }
    bool mkdir(const char*){ return true; }
    bool remove(const char*){ return true; }
    bool rename(const char*, const char*){ return true; }
  };
}
#define FILE_READ   "r"
#define FILE_APPEND "a"
namespace Store {
  inline fs::FS& fs(){ static fs::FS f; return f; }
  inline bool ready(){ return true; }
  inline bool onSD(){ return false; }
}
"""
    NET = ('RotctlRotator', 'PstRotator')     # see rot_body(): network, not ours
    NET_STUB = """
struct RotctlRotator : public Rotator {
  RotctlRotator(const char*, uint16_t) {}
  void begin() override {} bool ready() const override { return false; }
  bool point(float,float) override { return false; }
  bool readPos(float&,float&) override { return false; }
  void stop() override {} const char* name() const override { return "rotctl"; }
};
struct PstRotator : public Rotator {
  PstRotator(const char*, uint16_t) {}
  void begin() override {} bool ready() const override { return false; }
  bool point(float,float) override { return false; }
  bool readPos(float&,float&) override { return false; }
  void stop() override {} const char* name() const override { return "PST"; }
};
"""
    tu += (ROT_STUB + cfg_txt + '\n'
           + rot_body(ROT_H, NET) + '\n' + NET_STUB + '\n'
           + rot_body(ROT_CPP, NET) + '\n')
    # logstore AFTER cfg_txt: it needs LOG_DIR and LOG_MAX_BYTES_* from config.h.
    if os.path.isfile(LOG_H) and os.path.isfile(LOG_CPP):
        tu += (LOG_STUB + rot_body(LOG_H) + '\n' + rot_body(LOG_CPP) + '\n')
    # consolelog defines CardSatSerialTee, which config.h declares extern and
    # every Serial.print in the TU references. Without it the gate link-fails on
    # a symbol the real build resolves fine -- so include it rather than stub it.
    CON_H   = os.path.join(ROOT, 'src', 'consolelog.h')
    CON_CPP = os.path.join(ROOT, 'src', 'consolelog.cpp')
    if os.path.isfile(CON_H) and os.path.isfile(CON_CPP):
        tu += (rot_body(CON_H) + '\n' + rot_body(CON_CPP) + '\n')
tu += '\nint main(){ UsbSerial::end(); return (int)UsbSerial::hostTaskHeadroom(); }\n'

with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, 'gate.cpp')
    out = os.path.join(d, 'gate')
    open(src, 'w').write(tu)
    # Compile AND link: link catches the anon-namespace duplicate-decl trap
    # (decl inside `namespace {}` + `static` def outside = ambiguous overload).
    #
    # -Wall -Wextra is NOT decoration. The Arduino build ships `-w`, which
    # silences every warning the compiler has -- and 0.9.58 shipped a real bug
    # because of it: freeRotator() deleted its transport through a `Stream*`, and
    # Arduino's Stream has no virtual destructor, so ~UsbRotStream() (which
    # releases the USB CDC port) NEVER RAN. GCC said so four times, in a build
    # nobody could hear. This is the only place in the project where the compiler
    # is allowed to talk.
    p = subprocess.run(['g++', '-std=gnu++17', '-Wall', '-Wextra',
                        '-Wno-unused-parameter', '-Wno-unused-variable',
                        src, '-o', out],
                       capture_output=True, text=True)
    # Warnings that indicate a REAL defect in CardSat's own code. Kept narrow on
    # purpose: a gate that cries wolf gets ignored, and the stub's own modelling
    # (e.g. EspUsbHostCdcSerial deriving from Stream) produces false positives
    # that the device build does not have.
    FATAL = ('-Wdelete-non-virtual-dtor',)
    fatal = [l for l in p.stderr.splitlines()
             if any(f in l for f in FATAL) and 'EspUsbHostCdcSerial' not in l]
    if fatal:
        print('  compile gate: FAILED -- warning(s) that indicate real bugs:')
        for l in fatal[:6]:
            print('   ', l.replace(src, 'usbserial'))
        sys.exit(1)
    if p.returncode != 0:
        errs = [l for l in p.stderr.splitlines() if 'error' in l][:6]
        print('  compile gate: FAILED -- usbserial/rotator does not build:')
        for e in errs:
            print('   ', e.replace(src, 'usbserial'))
        sys.exit(1)

print('  compile gate: usbserial + serial-rotator sources compile and link cleanly.')
