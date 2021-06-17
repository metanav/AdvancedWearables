#include <stdio.h>
#include "display.h"
#include "sd.h"
#include "message.h"

#define LABELS_COUNT 4
#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(display);

LV_FONT_DECLARE(calibri_20);
LV_FONT_DECLARE(calibri_18);

extern struct k_mbox data_mailbox;
extern struct k_mbox notification_mailbox;

static lv_obj_t *btnm;
static lv_obj_t *mbox;
static lv_obj_t *status_label;

static lv_style_t style_label;
static lv_style_t style_btnm;
static lv_style_t style_mbox;
static lv_style_t style_err_mbox;

static int8_t btnm_states[LABELS_COUNT] = {0, 0, 0, 0};
static char labels[LABELS_COUNT][15] = {"Walking", "Running", "Sitting Good", "Sitting Bad"};
static char class_label[15];

static const char *btnm_map[] = {
    labels[0], labels[1], "\n", 
    labels[2], labels[3], ""
};

struct chart_type {
    lv_obj_t *chart_obj;
    lv_chart_series_t *series[3]; 
};

static bool recording = false;
static bool recording_ready = false;
static bool recording_done  = false;

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

void chart_update(struct chart_type *chart, uint16_t *data, bool refresh)
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

    lv_style_init(&style_btnm);
    lv_style_set_text_font(&style_btnm, LV_STATE_DEFAULT, &calibri_20);

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
    lv_label_set_text(status_label, "Press any button to start recording");
    lv_obj_align(status_label, NULL, LV_ALIGN_CENTER, 0, 15);
    lv_obj_add_style(status_label, LV_BTN_PART_MAIN, &style_label);
}

static void mbox_event_cb(lv_obj_t *obj, lv_event_t evt)
{
    if(evt == LV_EVENT_DELETE && obj == mbox) {
        LOG_INF("mbox: LV_EVENT_DELETE event was called\n"); 
        mbox = NULL;
        for (int8_t i=0; i<LABELS_COUNT; i++) {
            if (btnm_states[i] == 1) {
                strcpy(class_label, labels[i]); 
                recording = true;
                LOG_INF("mbox: Going to capture data for %s\n", labels[i]);
                break;
            }
        }
    } 
}

static void btn_matrix_event_handler(lv_obj_t *obj, lv_event_t event)
{
    uint16_t id = lv_btnmatrix_get_active_btn(obj);

    if(event == LV_EVENT_VALUE_CHANGED) {
        btnm_states[id] = !btnm_states[id];

        if (btnm_states[id] == 0) {
            if (mbox) {
                lv_msgbox_start_auto_close(mbox, 0);
            }

            for (uint8_t i=0; i < LABELS_COUNT; i++) {
                if (i != id) {
                    lv_btnmatrix_clear_btn_ctrl(obj, i, LV_BTNMATRIX_CTRL_DISABLED);
                }
            }

            if (recording) {
                // cancel recording
                recording = false;
                recording_done = false;
                recording_ready = false;

                mbox = lv_msgbox_create(lv_scr_act(), NULL);
                char str[50];
                sprintf(str, "Recording canceled", lv_btnmatrix_get_active_btn_text(obj));
                lv_msgbox_set_text(mbox, str);
                lv_msgbox_start_auto_close(mbox, 5000);
                lv_obj_set_size(mbox, LV_HOR_RES/2, LV_VER_RES/4);
                lv_obj_add_style(mbox, LV_BTN_PART_MAIN, &style_mbox);
                lv_obj_set_pos(mbox, LV_HOR_RES/4,  LV_VER_RES/2);
                lv_obj_set_style_local_bg_color(mbox, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, LV_COLOR_GREEN);

                lv_label_set_text(status_label, "Press any button to start recording");
            }
        } else {
            for (uint8_t i=0; i < LABELS_COUNT; i++) {
                if (i != id) {
                    lv_btnmatrix_set_btn_ctrl(obj, i, LV_BTNMATRIX_CTRL_DISABLED);
                }
            }

            mbox = lv_msgbox_create(lv_scr_act(), NULL);
            char str[50];
            sprintf(str, "[%s]\nStarting in 5s", lv_btnmatrix_get_active_btn_text(obj));
            lv_msgbox_set_text(mbox,str);
            lv_msgbox_start_auto_close(mbox, 5000);
            lv_obj_set_event_cb(mbox, mbox_event_cb);
            lv_obj_set_size(mbox, LV_HOR_RES/2, LV_VER_RES/4);
            lv_obj_add_style(mbox, LV_BTN_PART_MAIN, &style_mbox);
            lv_obj_set_pos(mbox, LV_HOR_RES/4,  LV_VER_RES/2);
            lv_obj_set_style_local_bg_color(mbox, LV_BTNMATRIX_PART_BG, LV_STATE_DEFAULT, LV_COLOR_GREEN);
        }
    }
}

lv_obj_t *err_mbox;

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

void create_btn_matrix(void)
{
    btnm = lv_btnmatrix_create(lv_scr_act(), NULL);
    lv_btnmatrix_set_map(btnm, btnm_map);
    lv_btnmatrix_set_btn_ctrl_all(btnm,  LV_BTNMATRIX_CTRL_CHECKABLE);

    lv_obj_add_style(btnm, LV_BTN_PART_MAIN, &style_btnm);
    lv_obj_align(btnm, NULL, LV_ALIGN_IN_TOP_MID, 0, 0);
    lv_obj_set_event_cb(btnm, btn_matrix_event_handler);
    lv_obj_set_style_local_bg_color(btnm, LV_BTNMATRIX_PART_BTN, LV_STATE_DEFAULT, LV_COLOR_BLUE);
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
    create_btn_matrix();
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
    int rec_i = 0;
    const int rec_buf_size = 4500;
    int16_t rec_buf[rec_buf_size];

    while (1) {
        if (recording == true && recording_ready == false) {
             recording_ready = true;
             memset(rec_buf, 0, rec_buf_size); 
             rec_i = 0;
        }

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

                if (recording && recording_ready) {
                    rec_buf[rec_i++] = data[0];
                    rec_buf[rec_i++] = data[1];
                    rec_buf[rec_i++] = data[2];

                    if (rec_i > rec_buf_size-1) {
                        recording_done = true;
                    }
                }
            }

            if (recording && recording_ready) {        
                lv_label_set_text_fmt(status_label, "Recording: %d samples", rec_i/3);
                LOG_INF("Recording: %d samples",  rec_i/3);
            }
        }

       if (recording && recording_done) {
            recording = false;
            recording_done = false;
            recording_ready = false;

            LOG_INF("Writing to SD..\n");

            if (!write_to_sd(class_label, rec_buf, rec_buf_size)) {
                lv_label_set_text(status_label, "Write to SD card failed.");
            } else {
                lv_label_set_text(status_label, "Data saved to SD card.");
            }
       }

        lv_task_handler();
        k_sleep(K_MSEC(5));
    }
}

void display_init() 
{
    k_sleep(K_MSEC(1000));

    if (!sd_init()) {
        enum noti_message msg = BLE_PERIPHERAL_UNSUBSCRIBED;
        show_err_mbox(msg);
    }

    LOG_INF("Display initialized\n");
}

