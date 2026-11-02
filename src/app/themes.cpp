// Mirrorly - theme registry.
//
// To add a theme: write src/app/theme_<name>.cpp (a class deriving from Theme plus a
// make<Name>Theme() factory), then add the factory below. Themes appear in Settings in
// this order; the first one is the default.
#include "app.h"

Theme* makeDefaultTheme();
Theme* makePosterTheme();

std::vector<Theme*>& allThemes() {
    static std::vector<Theme*> list = {
        makeDefaultTheme(),
        makePosterTheme(),
    };
    return list;
}

Theme* findTheme(const std::wstring& id) {
    for (Theme* t : allThemes())
        if (id == t->id()) return t;
    return allThemes().front();
}
