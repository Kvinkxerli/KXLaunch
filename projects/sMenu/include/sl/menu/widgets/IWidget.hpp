#pragma once
#include <string>
#include <vector>

namespace sl::menu::widgets {

    class IWidget {
    public:
        virtual ~IWidget() = default;
        virtual std::string Name() const { return ""; }
        virtual std::string Description() const { return ""; }
        virtual void Update() {}
        template<typename... Args>
        int Render(Args&&...) { return 0; }
        virtual int  OptionCount() const { return 0; }
        virtual std::string OptionType(int) const { return ""; }
        virtual std::string OptionLabel(int) const { return ""; }
        virtual std::string OptionValue(int) const { return ""; }
        virtual void SetOption(int, const std::string &) {}
        virtual int  ConfigCount() const { return 0; }
        virtual const char *ConfigKey(int) const { return ""; }
        virtual const char *ConfigValue(int) const { return ""; }
        virtual void SetConfig(int, const char *) {}
    };

} // namespace sl::menu::widgets
