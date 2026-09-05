#include "input_ui_bridge.h"

namespace InputUiBridge {

void routeToUi(const Input::Event &event, Ui &ui) {
  Ui::Event uiEvent;
  uiEvent.value = event.value;

  switch (event.type) {
    case Input::EventType::KeypadNoteOn:
      // Routed through Midi by SamplerCallbackBinder, including playback.
      return;
    case Input::EventType::LeftRotate:
      uiEvent.type = Ui::EventType::LeftRotate;
      break;
    case Input::EventType::LeftClick:
      uiEvent.type = Ui::EventType::LeftClick;
      break;
    case Input::EventType::RightRotate:
      uiEvent.type = Ui::EventType::RightRotate;
      break;
    case Input::EventType::RightClick:
      uiEvent.type = Ui::EventType::RightClick;
      break;
    case Input::EventType::RightLongPress:
      uiEvent.type = Ui::EventType::RightLongPress;
      break;
  }

  ui.handleEvent(uiEvent);
}

}  // namespace InputUiBridge
