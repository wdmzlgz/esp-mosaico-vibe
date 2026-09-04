// SPDX-License-Identifier: Apache-2.0
#include "mosaico_game_input.h"
bool mosaico_game_input_pointer(int32_t x,int32_t y,bool pressed,uint64_t timestamp_us){
    mosaico_device_event_t e={.type=MOSAICO_DEVICE_EVENT_POINTER,.x=x,.y=y,.pressed=pressed,.timestamp_us=timestamp_us};
    return MosaicoGamePostDeviceEvent(&e);
}
bool mosaico_game_input_touch(int32_t track_id,int32_t x,int32_t y,bool pressed,uint64_t timestamp_us){
    mosaico_device_event_t e={.type=MOSAICO_DEVICE_EVENT_TOUCH,.x=x,.y=y,
        .value=track_id,.pressed=pressed,.timestamp_us=timestamp_us};
    return MosaicoGamePostDeviceEvent(&e);
}
bool mosaico_game_input_button(int32_t button,bool pressed,uint64_t timestamp_us){
    mosaico_device_event_t e={.type=MOSAICO_DEVICE_EVENT_BUTTON,.value=button,.pressed=pressed,.timestamp_us=timestamp_us};
    return MosaicoGamePostDeviceEvent(&e);
}
bool mosaico_game_input_joystick(int32_t x,int32_t y,uint64_t timestamp_us){
    mosaico_device_event_t e={.type=MOSAICO_DEVICE_EVENT_JOYSTICK,.x=x,.y=y,.timestamp_us=timestamp_us};
    return MosaicoGamePostDeviceEvent(&e);
}
