#include <stdio.h>
#include "display.h"
#include "message.h"

#include "ei_run_classifier.h"
#include "numpy.hpp"

#define LABELS_COUNT 4
#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(display);

LV_FONT_DECLARE(calibri_32b);
LV_FONT_DECLARE(calibri_20);
LV_FONT_DECLARE(calibri_18);

extern struct k_mbox data_mailbox;
extern struct k_mbox notification_mailbox;

static lv_obj_t *mbox;
lv_obj_t *err_mbox;
static lv_obj_t *status_label;
static lv_obj_t *recognition_label;

static lv_style_t style_label;
static lv_style_t style_mbox;
static lv_style_t style_err_mbox;
static lv_style_t style_recognition_label;

static char labels[LABELS_COUNT][15] = {"Walking", "Running", "Sitting Good", "Sitting Bad"};

struct chart_type {
    lv_obj_t *chart_obj;
    lv_chart_series_t *series[3]; 
};

void chart_init(struct chart_type *chart)
{   
    /* Create a chart */
    chart->chart_obj = lv_chart_create(lv_scr_act(), NULL);
    lv_obj_set_size(chart->chart_obj, 310, 80);
    lv_obj_align(chart->chart_obj, NULL, LV_ALIGN_IN_BOTTOM_MID, 0, -5);
    lv_chart_set_type(chart->chart_obj, LV_CHART_TYPE_LINE);   /*Show lines and points too*/
    // no grids
    lv_chart_set_div_line_count(chart->chart_obj, 0, 0);
    lv_chart_set_point_count(chart->chart_obj, 100);
    lv_obj_set_style_local_bg_color(chart->chart_obj, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_COLOR_BLACK);          
    lv_obj_set_style_local_border_color(chart->chart_obj, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_COLOR_BLACK);          

    // no points only lines
    lv_obj_set_style_local_size(chart->chart_obj, LV_CHART_PART_SERIES, LV_STATE_DEFAULT, 0);
    lv_chart_set_y_range(chart->chart_obj, LV_CHART_AXIS_PRIMARY_Y, -1536, 1536);

    /* Add three data series */
    chart->series[0] = lv_chart_add_series(chart->chart_obj, LV_COLOR_RED);
    chart->series[1] = lv_chart_add_series(chart->chart_obj, LV_COLOR_GREEN);
    chart->series[2] = lv_chart_add_series(chart->chart_obj, LV_COLOR_BLUE);
}

void chart_update(struct chart_type *chart, int16_t *data, bool refresh)
{
    lv_chart_set_next(chart->chart_obj, chart->series[0], data[0]);
    lv_chart_set_next(chart->chart_obj, chart->series[1], data[1]);
    lv_chart_set_next(chart->chart_obj, chart->series[2], data[2]);
    if (refresh) {
        lv_chart_refresh(chart->chart_obj);
    }
}

static void style_init()
{
    lv_style_init(&style_label);
    lv_style_set_text_font(&style_label, LV_STATE_DEFAULT, &calibri_20);

    lv_style_init(&style_recognition_label);
    lv_style_set_text_font(&style_recognition_label, LV_STATE_DEFAULT, &calibri_32b);

    lv_style_init(&style_mbox);
    lv_style_set_text_font(&style_mbox, LV_STATE_DEFAULT, &calibri_18);
    lv_style_set_text_color(&style_mbox, LV_STATE_DEFAULT, LV_COLOR_BLACK);

    lv_style_init(&style_err_mbox);
    lv_style_set_text_font(&style_err_mbox, LV_STATE_DEFAULT, &calibri_18);
    lv_style_set_text_color(&style_mbox, LV_STATE_DEFAULT, LV_COLOR_BLACK);
}

static void create_label()
{
    status_label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(status_label, "");
    lv_obj_align(status_label, NULL, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_style(status_label, LV_BTN_PART_MAIN, &style_label);
}

static void create_recognition_label()
{
    recognition_label = lv_label_create(lv_scr_act(), NULL);
    lv_label_set_text(recognition_label, "Please wait for data.");
    lv_obj_align(recognition_label, NULL, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_style(recognition_label, LV_BTN_PART_MAIN, &style_recognition_label);
}

static void mbox_event_cb(lv_obj_t *obj, lv_event_t evt)
{
    if(evt == LV_EVENT_DELETE && obj == mbox) {
        LOG_INF("mbox: LV_EVENT_DELETE event was called\n"); 
        mbox = NULL;
    } 
}

static void err_mbox_event_cb(lv_obj_t *obj, lv_event_t evt)
{
     LOG_INF("err_mbox_event_cb was called\n");

     if(evt == LV_EVENT_VALUE_CHANGED) {
        lv_msgbox_start_auto_close(err_mbox, 0);
     }
}

void show_err_mbox(enum noti_message msg)
{
    char message[30];
    bool auto_close;

    LOG_INF("msg=%d\n", msg);

    switch (msg)
    {
        case BLE_PERIPHERAL_DISCONNECTED:
            strcpy(message, "BLE PERIPHERAL DISCONNECTED");  
            auto_close = true;
            break;
        case BLE_PERIPHERAL_CONNECTED:
            strcpy(message, "BLE PERIPHERAL CONNECTED");  
            auto_close = true;
            break;
        case BLE_PERIPHERAL_UNSUBSCRIBED:
            strcpy(message, "BLE PERIPHERAL UNSUBSCRIBED");  
            auto_close = true;
            break;
        case BLE_PERIPHERAL_SUBSCRIBED:
            strcpy(message, "BLE PERIPHERAL SUBSCRIBED");  
            auto_close = true;
            break;
        case BLE_PERIPHERAL_SCANNING:
            strcpy(message, "BLE PERIPHERAL SCANNING");  
            auto_close = true;
            break;
        case BLE_PERIPHERAL_FOUND:
            strcpy(message, "BLE PERIPHERAL FOUND");  
            auto_close = true;
            break;
        case SD_CARD_ERROR:
            strcpy(message, "Check SD card");  
            auto_close = false;
            break;
        default:
            strcpy(message, "Something gone wrong!");  
            auto_close = false;
    }

    LOG_DBG("message=%s\n", message);

    err_mbox = lv_msgbox_create(lv_scr_act(), NULL);
    static const char * btns[] = {"OK", ""};

    lv_msgbox_set_text(err_mbox, message);
    
    lv_obj_set_size(err_mbox, (LV_HOR_RES*3)/4, LV_VER_RES/4);
    lv_obj_add_style(err_mbox, LV_BTN_PART_MAIN, &style_err_mbox);
    lv_obj_align(err_mbox, NULL, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_local_bg_color(err_mbox, LV_MSGBOX_PART_BG, LV_STATE_DEFAULT, LV_COLOR_GREEN);

    if (auto_close) {
        lv_msgbox_start_auto_close(err_mbox, 5000);
    } else {
        lv_msgbox_add_btns(err_mbox, btns);
        lv_obj_set_event_cb(err_mbox, err_mbox_event_cb);
    }

}

static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE] = {0.0f};

int raw_feature_get_data(size_t offset, size_t length, float *out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}

void display_entrypoint(void)
{
    const struct device *display_dev;
    display_dev = device_get_binding(CONFIG_LVGL_DISPLAY_DEV_NAME);

    if (display_dev == NULL) {
        LOG_ERR("%s device not found.  Aborting test.", CONFIG_LVGL_DISPLAY_DEV_NAME);
        return;
    }

    style_init();
    display_blanking_off(display_dev);
    create_recognition_label();
    create_label();

    LOG_INF("Display %s initialized\n", CONFIG_LVGL_DISPLAY_DEV_NAME);

    struct k_mbox_msg recv_n_msg;
    struct k_mbox_msg recv_d_msg;
    struct chart_type acc_chart;
    chart_init(&acc_chart);
    uint8_t count = 0;
    bool refresh;
    uint8_t buffer[150];
    int16_t data[3];


    // This is needed so that output of printf is output immediately without buffering
    setvbuf(stdout, NULL, _IONBF, 0);

    LOG_INF("Edge Impulse inferencing \n");

    if (sizeof(features) / sizeof(float) != EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE) {
        LOG_INF("The size of your 'features' array is not correct. Expected %d items, but had %u\n",
            EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE, sizeof(features) / sizeof(float));
        return;
    }

    ei_impulse_result_t result = { 0 };

    uint8_t data_block= 0;
    bool do_inferencing = false;

    while (1) {
        recv_n_msg.rx_source_thread = K_ANY;

        if (0 == k_mbox_get(&notification_mailbox, &recv_n_msg, NULL, K_NO_WAIT)) {
            LOG_INF("recv_n_msg.info=%d\n", recv_n_msg.info);
            show_err_mbox((enum noti_message) recv_n_msg.info);
        }

        refresh = false;
        recv_d_msg.size = 150;
        recv_d_msg.rx_source_thread = K_ANY;

        if (0 == k_mbox_get(&data_mailbox, &recv_d_msg, NULL, K_NO_WAIT)) {
            k_mbox_data_get(&recv_d_msg, buffer);

            LOG_DBG("recv_msg size=%d\n", recv_d_msg.size);

            for (uint8_t i = 0; i < 150; i += 6) {
                data[0] = buffer[i+0] | (buffer[i+1] << 8);
                data[1] = buffer[i+2] | (buffer[i+3] << 8);
                data[2] = buffer[i+4] | (buffer[i+5] << 8);

                if (++count == 25) {
                    count = 0;
                    refresh = true;
                }

                chart_update(&acc_chart, data, refresh);
                
                uint8_t j = (data_block == 0) ? i/2 : 75 + (i/2);
                features[j+0] = data[0];
                features[j+1] = data[1];
                features[j+2] = data[2];
            }
           
            do_inferencing = (data_block == 1) ? true : false;
            
            data_block ^= 1;
        }

        if (do_inferencing) {
            // the features are stored into flash, and we don't want to load everything into RAM
            signal_t features_signal;
            features_signal.total_length = sizeof(features) / sizeof(features[0]);
            features_signal.get_data = &raw_feature_get_data;

            // invoke the impulse
            EI_IMPULSE_ERROR res = run_classifier(&features_signal, &result, true);
            LOG_INF("run_classifier returned: %d\n", res);

            if (res != 0) return;

            LOG_INF("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.): \n",
                result.timing.dsp, result.timing.classification, result.timing.anomaly);

            // print the predictions
            LOG_INF("[");
            uint8_t max_ix;
            float max_value = 0.0f;
            for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
                if (result.classification[ix].value > max_value) {
                    max_value = result.classification[ix].value;
                    max_ix = ix;
                }
                ei_printf_float(result.classification[ix].value);
                if (ix != EI_CLASSIFIER_LABEL_COUNT - 1) {
                    LOG_INF(", ");
                }
            }
            LOG_INF("]\n");
            lv_label_set_text(recognition_label, result.classification[max_ix].label);
        }

        lv_task_handler();
    }
}

void display_init() 
{
    k_sleep(K_MSEC(1000));
    LOG_INF("Display initialized\n");
}

