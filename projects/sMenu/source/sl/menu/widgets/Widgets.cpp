#include <sl/menu/widgets/Widgets.hpp>

namespace sl::menu::widgets {

    void Widgets::Init() {}
    void Widgets::Exit() {}
    void Widgets::SetEnabled(int, bool) {}
    void Widgets::Render(gfx::Gfx *, const ui::Theme &, bool) {}
    int  Widgets::HitTest(int, int) const { return -1; }
    bool Widgets::GetBox(int, int &, int &, int &, int &) const { return false; }
    void Widgets::MoveBy(int, int, int) {}
    void Widgets::SavePositions() {}
    void Widgets::ThreadTrampoline(void *) {}
    void Widgets::ThreadLoop() {}
    void Widgets::LoadWidgets() {}
    void Widgets::LoadPositions() {}
    void Widgets::LoadEnabled() {}

} // namespace sl::menu::widgets
