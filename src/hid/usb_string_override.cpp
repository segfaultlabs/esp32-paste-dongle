// USB string descriptor override — currently a no-op.
//
// tud_descriptor_string_cb is __attribute__((weak)) in esp32-hal-tinyusb.c,
// so a definition here would override it. We deliberately do NOT define it
// so the framework's default implementation handles all string indices,
// including the ones set by USB.manufacturerName() / USB.productName().
//
// The "TinyUSB HID" interface string (index varies by init order) is a minor
// disclosure. The main identifiers (VID, PID, manufacturer, product) are
// already handled by set_identity() → USB.VID() / USB.manufacturerName() etc.
// in usb_hid.cpp::begin().
//
// TODO: properly intercept the interface string by reading the framework's
// internal tinyusb_string_descriptor[] table and blanking "TinyUSB *" entries.
