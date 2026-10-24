#include <sl/menu/usb/Mtp.hpp>

namespace sl::menu::usb {

    void MtpStart() {}
    void MtpStop() {}
    bool MtpConnected() { return false; }
    void MtpGetStatus(MtpStatus &out) { out = {}; }

} // namespace sl::menu::usb
