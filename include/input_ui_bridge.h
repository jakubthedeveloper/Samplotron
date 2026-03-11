#pragma once

#include "input.h"
#include "ui.h"

namespace InputUiBridge {

void routeToUi(const Input::Event &event, Ui &ui);

}  // namespace InputUiBridge
