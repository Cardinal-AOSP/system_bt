/******************************************************************************
 *
 *  Copyright (C) 2014  Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/

/*******************************************************************************
 *
 *  Filename:      btif_gatt_multi_adv_util.c
 *
 *  Description:   Multi ADV helper implementation
 *
 *******************************************************************************/

#define LOG_TAG "bt_btif_gatt"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btu.h"
#include "bt_target.h"

#if (BLE_INCLUDED == TRUE)

#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>

#include "bta_gatt_api.h"
#include "btif_common.h"
#include "btif_gatt_multi_adv_util.h"
#include "btif_gatt_util.h"

using std::vector;

extern fixed_queue_t *btu_general_alarm_queue;

/*******************************************************************************
**  Static variables
********************************************************************************/
static int user_app_count = 0;
static btgatt_multi_adv_common_data *p_multi_adv_com_data_cb = NULL;

btgatt_multi_adv_common_data *btif_obtain_multi_adv_data_cb()
{
    int max_adv_inst = BTM_BleMaxMultiAdvInstanceCount();
    if (0 == max_adv_inst)
        max_adv_inst = 1;

    BTIF_TRACE_DEBUG("%s, Count:%d", __func__, max_adv_inst);

    if (NULL == p_multi_adv_com_data_cb)
    {
        p_multi_adv_com_data_cb = (btgatt_multi_adv_common_data*)osi_calloc(sizeof(btgatt_multi_adv_common_data));
        /* Storing both advertiser_id and inst_id details */

        p_multi_adv_com_data_cb->inst_cb = (btgatt_multi_adv_inst_cb*)
            osi_calloc((max_adv_inst + 1) * sizeof(btgatt_multi_adv_inst_cb));
    }

    return p_multi_adv_com_data_cb;
}

void btif_gattc_incr_app_count(void)
{
    // TODO: Instead of using a fragile reference counter here, one could
    //       simply track the advertiser_id instances that are in the map.
    ++user_app_count;
}

void btif_gattc_decr_app_count(void)
{
    if (user_app_count > 0)
        user_app_count--;

    if ((user_app_count == 0) && (p_multi_adv_com_data_cb != NULL)) {
       osi_free(p_multi_adv_com_data_cb->inst_cb);
       osi_free_and_reset((void **)&p_multi_adv_com_data_cb);
    }
}

void btif_gattc_adv_data_packager(int advertiser_id, bool set_scan_rsp,
                bool include_name, bool include_txpower, int min_interval, int max_interval,
                int appearance, const vector<uint8_t> &manufacturer_data,
                const vector<uint8_t> &service_data, const vector<uint8_t> &service_uuid,
                btif_adv_data_t *p_multi_adv_inst)
{
    memset(p_multi_adv_inst, 0 , sizeof(btif_adv_data_t));

    p_multi_adv_inst->advertiser_id = (uint8_t) advertiser_id;
    p_multi_adv_inst->set_scan_rsp = set_scan_rsp;
    p_multi_adv_inst->include_name = include_name;
    p_multi_adv_inst->include_txpower = include_txpower;
    p_multi_adv_inst->min_interval = min_interval;
    p_multi_adv_inst->max_interval = max_interval;
    p_multi_adv_inst->appearance = appearance;

    if (manufacturer_data.size() > 0)
    {
        size_t manufacturer_len = manufacturer_data.size();
        if (manufacturer_len > MAX_SIZE_MANUFACTURER_DATA)
            manufacturer_len = MAX_SIZE_MANUFACTURER_DATA;

        p_multi_adv_inst->manufacturer_len = manufacturer_len;
        memcpy(p_multi_adv_inst->p_manufacturer_data, manufacturer_data.data(), manufacturer_len);
    }

    if (service_data.size() > 0)
    {
        size_t service_data_len = service_data.size();
        if (service_data_len > MAX_SIZE_PROPRIETARY_ELEMENT)
            service_data_len = MAX_SIZE_PROPRIETARY_ELEMENT;

        p_multi_adv_inst->service_data_len = service_data_len;
        memcpy(p_multi_adv_inst->p_service_data, service_data.data(), service_data_len);
    }

    if (service_uuid.size() > 0)
    {
        size_t service_uuid_len = service_uuid.size();
        if (service_uuid_len > MAX_SIZE_SERVICE_DATA)
            service_uuid_len = MAX_SIZE_SERVICE_DATA;

        p_multi_adv_inst->service_uuid_len = service_uuid_len;
        memcpy(p_multi_adv_inst->p_service_uuid, service_uuid.data(), service_uuid_len);
    }
}

bool btif_gattc_copy_datacb(int cbindex, const btif_adv_data_t *p_adv_data,
                               bool bInstData) {
    btgatt_multi_adv_common_data *p_multi_adv_data_cb = btif_obtain_multi_adv_data_cb();
    if (NULL == p_multi_adv_data_cb || cbindex < 0)
       return false;

    BTIF_TRACE_DEBUG("%s", __func__);

    memset(&p_multi_adv_data_cb->inst_cb[cbindex].data, 0,
           sizeof(p_multi_adv_data_cb->inst_cb[cbindex].data));
    p_multi_adv_data_cb->inst_cb[cbindex].mask = 0;

    if (!p_adv_data->set_scan_rsp)
    {
         p_multi_adv_data_cb->inst_cb[cbindex].mask = BTM_BLE_AD_BIT_FLAGS;
         p_multi_adv_data_cb->inst_cb[cbindex].data.flag = ADV_FLAGS_GENERAL;
         if (p_multi_adv_data_cb->inst_cb[cbindex].timeout_s)
             p_multi_adv_data_cb->inst_cb[cbindex].data.flag = ADV_FLAGS_LIMITED;
         if (p_multi_adv_data_cb->inst_cb[cbindex].param.adv_type == BTA_BLE_NON_CONNECT_EVT)
             p_multi_adv_data_cb->inst_cb[cbindex].data.flag &=
                    ~(BTA_DM_LIMITED_DISC | BTA_DM_GENERAL_DISC);
         if (p_multi_adv_data_cb->inst_cb[cbindex].data.flag == 0)
            p_multi_adv_data_cb->inst_cb[cbindex].mask = 0;
    }

    if (p_adv_data->include_name)
        p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_DEV_NAME;

    if (p_adv_data->include_txpower)
        p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_TX_PWR;

    if (false == bInstData && p_adv_data->min_interval > 0 && p_adv_data->max_interval > 0 &&
        p_adv_data->max_interval > p_adv_data->min_interval)
    {
        p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_INT_RANGE;
        p_multi_adv_data_cb->inst_cb[cbindex].data.int_range.low =
                                        p_adv_data->min_interval;
        p_multi_adv_data_cb->inst_cb[cbindex].data.int_range.hi =
                                        p_adv_data->max_interval;
    }
    else
    if (true == bInstData)
    {
        if (p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_min > 0 &&
            p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_max > 0 &&
            p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_max >
            p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_min)
        {
              p_multi_adv_data_cb->inst_cb[cbindex].data.int_range.low =
              p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_min;
              p_multi_adv_data_cb->inst_cb[cbindex].data.int_range.hi =
              p_multi_adv_data_cb->inst_cb[cbindex].param.adv_int_max;
        }

        if (p_adv_data->include_txpower)
        {
            p_multi_adv_data_cb->inst_cb[cbindex].data.tx_power =
                p_multi_adv_data_cb->inst_cb[cbindex].param.tx_power;
        }
    }

    if (p_adv_data->appearance != 0)
    {
        p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_APPEARANCE;
        p_multi_adv_data_cb->inst_cb[cbindex].data.appearance = p_adv_data->appearance;
    }

    if (p_adv_data->manufacturer_len > 0 &&
        p_adv_data->manufacturer_len < MAX_SIZE_MANUFACTURER_DATA)
    {
      p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_MANU;
      p_multi_adv_data_cb->inst_cb[cbindex].data.manu.len =
          p_adv_data->manufacturer_len;
      memcpy(&p_multi_adv_data_cb->inst_cb[cbindex].data.manu.val,
             p_adv_data->p_manufacturer_data, p_adv_data->manufacturer_len);
    }

    if (p_adv_data->service_data_len > 0 &&
        p_adv_data->service_data_len < MAX_SIZE_PROPRIETARY_ELEMENT)
    {
      BTIF_TRACE_DEBUG("%s - In service_data", __func__);
      tBTA_BLE_PROPRIETARY *p_prop = &p_multi_adv_data_cb->inst_cb[cbindex].data.proprietary;
      p_prop->num_elem = 1;

      tBTA_BLE_PROP_ELEM *p_elem = &p_prop->elem[0];
      p_elem->adv_type = BTM_BLE_AD_TYPE_SERVICE_DATA;
      p_elem->len = p_adv_data->service_data_len;
      memcpy(p_elem->val, p_adv_data->p_service_data,
             p_adv_data->service_data_len);

      p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_PROPRIETARY;
    }

    if (p_adv_data->service_uuid_len)
    {
        uint16_t *p_uuid_out16 = NULL;
        uint32_t *p_uuid_out32 = NULL;
        for (int position = 0; position < p_adv_data->service_uuid_len; position += LEN_UUID_128)
        {
             bt_uuid_t uuid;
             memset(&uuid, 0, sizeof(uuid));
             memcpy(&uuid.uu, p_adv_data->p_service_uuid + position, LEN_UUID_128);

             tBT_UUID bt_uuid;
             memset(&bt_uuid, 0, sizeof(bt_uuid));
             btif_to_bta_uuid(&bt_uuid, &uuid);

             switch(bt_uuid.len)
             {
                case (LEN_UUID_16):
                {
                  if (p_multi_adv_data_cb->inst_cb[cbindex].data.services.num_service == 0)
                  {
                      p_multi_adv_data_cb->inst_cb[cbindex].data.services.list_cmpl = false;
                      p_uuid_out16 = p_multi_adv_data_cb->inst_cb[cbindex].data.services.uuid;
                  }

                  if (p_multi_adv_data_cb->inst_cb[cbindex].data.services.num_service < MAX_16BIT_SERVICES)
                  {
                     BTIF_TRACE_DEBUG("%s - In 16-UUID_data", __func__);
                     p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_SERVICE;
                     ++p_multi_adv_data_cb->inst_cb[cbindex].data.services.num_service;
                     *p_uuid_out16++ = bt_uuid.uu.uuid16;
                  }
                  break;
                }

                case (LEN_UUID_32):
                {
                   if (p_multi_adv_data_cb->inst_cb[cbindex].data.service_32b.num_service == 0)
                   {
                      p_multi_adv_data_cb->inst_cb[cbindex].data.service_32b.list_cmpl = false;
                      p_uuid_out32 = p_multi_adv_data_cb->inst_cb[cbindex].data.service_32b.uuid;
                   }

                   if (p_multi_adv_data_cb->inst_cb[cbindex].data.service_32b.num_service < MAX_32BIT_SERVICES)
                   {
                      BTIF_TRACE_DEBUG("%s - In 32-UUID_data", __func__);
                      p_multi_adv_data_cb->inst_cb[cbindex].mask |= BTM_BLE_AD_BIT_SERVICE_32;
                      ++p_multi_adv_data_cb->inst_cb[cbindex].data.service_32b.num_service;
                      *p_uuid_out32++ = bt_uuid.uu.uuid32;
                   }
                   break;
                }

                case (LEN_UUID_128):
                {
                   /* Currently, only one 128-bit UUID is supported */
                   if (p_multi_adv_data_cb->inst_cb[cbindex].data.services_128b.num_service == 0)
                   {
                     BTIF_TRACE_DEBUG("%s - In 128-UUID_data", __func__);
                     p_multi_adv_data_cb->inst_cb[cbindex].mask |=
                         BTM_BLE_AD_BIT_SERVICE_128;
                     memcpy(p_multi_adv_data_cb->inst_cb[cbindex]
                                .data.services_128b.uuid128,
                            bt_uuid.uu.uuid128, LEN_UUID_128);
                     BTIF_TRACE_DEBUG(
                         "%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x,%x",
                         bt_uuid.uu.uuid128[0], bt_uuid.uu.uuid128[1],
                         bt_uuid.uu.uuid128[2], bt_uuid.uu.uuid128[3],
                         bt_uuid.uu.uuid128[4], bt_uuid.uu.uuid128[5],
                         bt_uuid.uu.uuid128[6], bt_uuid.uu.uuid128[7],
                         bt_uuid.uu.uuid128[8], bt_uuid.uu.uuid128[9],
                         bt_uuid.uu.uuid128[10], bt_uuid.uu.uuid128[11],
                         bt_uuid.uu.uuid128[12], bt_uuid.uu.uuid128[13],
                         bt_uuid.uu.uuid128[14], bt_uuid.uu.uuid128[15]);
                     ++p_multi_adv_data_cb->inst_cb[cbindex]
                           .data.services_128b.num_service;
                     p_multi_adv_data_cb->inst_cb[cbindex]
                         .data.services_128b.list_cmpl = true;
                   }
                   break;
                }

                default:
                     break;
             }
        }
    }

     return true;
}

void btif_gattc_clear_clientif(int advertiser_id, bool stop_timer)
{
    btgatt_multi_adv_common_data *p_multi_adv_data_cb = btif_obtain_multi_adv_data_cb();
    if (NULL == p_multi_adv_data_cb)
        return;

    btif_gattc_cleanup_inst_cb(advertiser_id, stop_timer);
}

void btif_gattc_cleanup_inst_cb(int inst_id, bool stop_timer)
{
    // Check for invalid instance id
    if (inst_id < 0 || inst_id >= BTM_BleMaxMultiAdvInstanceCount())
        return;

    btgatt_multi_adv_common_data *p_multi_adv_data_cb = btif_obtain_multi_adv_data_cb();
    if (NULL == p_multi_adv_data_cb)
        return;

    BTIF_TRACE_DEBUG("%s: inst_id %d", __func__, inst_id);
    btif_gattc_cleanup_multi_inst_cb(&p_multi_adv_data_cb->inst_cb[inst_id], stop_timer);
}

void btif_gattc_cleanup_multi_inst_cb(btgatt_multi_adv_inst_cb *p_multi_inst_cb,
                                             bool stop_timer)
{
    if (p_multi_inst_cb == NULL)
        return;

    // Discoverability timer cleanup
    if (stop_timer)
    {
        alarm_free(p_multi_inst_cb->multi_adv_timer);
        p_multi_inst_cb->multi_adv_timer = NULL;
    }

    memset(&p_multi_inst_cb->data, 0, sizeof(p_multi_inst_cb->data));
}

void btif_multi_adv_timer_ctrl(int advertiser_id, alarm_callback_t cb)
{
    btgatt_multi_adv_common_data *p_multi_adv_data_cb = btif_obtain_multi_adv_data_cb();
    if (p_multi_adv_data_cb == NULL)
        return;

    btgatt_multi_adv_inst_cb *inst_cb = &p_multi_adv_data_cb->inst_cb[advertiser_id];
    if (cb == NULL)
    {
        alarm_free(inst_cb->multi_adv_timer);
        inst_cb->multi_adv_timer = NULL;
    } else {
        if (inst_cb->timeout_s != 0)
        {
            alarm_free(inst_cb->multi_adv_timer);
            inst_cb->multi_adv_timer = alarm_new("btif_gatt.multi_adv_timer");
            alarm_set_on_queue(inst_cb->multi_adv_timer,
                               inst_cb->timeout_s * 1000,
                               cb, INT_TO_PTR(advertiser_id),
                               btu_general_alarm_queue);
        }
    }
}

#endif