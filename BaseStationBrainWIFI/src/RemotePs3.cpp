#include "RemotePs3.h"


RemotePs3::RemotePs3()
{
    memset(cur_state.data, 0, 10);
    memset(last_state.data, 0, 10);
}

void RemotePs3::update(char* data)
{
    swap();
    memcpy(cur_state.data,data,10);

    if(last_state.part.btn_select == false && cur_state.part.btn_select == true && _callback_select) _callback_select();

}

void RemotePs3::swap()
{
    memcpy(last_state.data,cur_state.data,10);
}

void RemotePs3::onSelect(callbackPS_t value)
{
    _callback_select = value;
}