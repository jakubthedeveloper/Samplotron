#pragma once

class DisplaySsd1309 {
 public:
  bool begin();
  void setBootStatus(bool sdOk, bool codecOk);
  void setLastSample(int sampleNumber);
  void update();

 private:
  void render();

  bool ready_ = false;
  bool dirty_ = true;
  bool sdOk_ = false;
  bool codecOk_ = false;
  int lastSample_ = 0;
};
