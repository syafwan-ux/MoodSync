#include "webui.hpp"
#include <iostream>

int main() {

    webui::window win;


    win.set_root_folder("application");
    win.show("index.html");
    webui::wait();
    return 0;
}