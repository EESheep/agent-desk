#include "panel.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

LV_FONT_DECLARE(font_cjk_28);
static cJSON *snapshot;
static int64_t received_us;
static double received_age, server_time;
static bool wifi_ok, http_ok, configured, offline;
static int page, list_page, provider_index, window_index;
static bool choosing;
static char selected_id[256], selected_source[16];
static lv_obj_t *header, *body, *connection_label, *nav[2];
static lv_obj_t *rows[2], *titles[2], *states[2], *pager, *previous, *next;
static lv_obj_t *detail_title, *detail_state, *quota_value, *quota_note, *quota_caption;
static lv_obj_t *quota_bar, *window_label, *quota_bucket;

static const char *str(const cJSON *obj, const char *key) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : "";
}
static double num(const cJSON *obj, const char *key, double fallback) {
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsNumber(v) && isfinite(v->valuedouble) ? v->valuedouble : fallback;
}
static cJSON *tasks(void) { return cJSON_GetObjectItemCaseSensitive(snapshot, "tasks"); }
static cJSON *providers(void) { return cJSON_GetObjectItemCaseSensitive(snapshot, "providers"); }
static const char *source_name(const char *s) {
    return !strcmp(s,"codex") ? "Codex" : !strcmp(s,"kimi") ? "Kimi" : "DSH";
}
static const char *state_name(const char *s) {
    return !strcmp(s,"active") ? "运行中" : !strcmp(s,"waiting") ? "等待输入" :
           !strcmp(s,"completed") ? "回合结束" : !strcmp(s,"systemError") ? "失败" : !strcmp(s,"idle") ? "空闲" : "未知";
}
static lv_color_t state_color(const char *s) {
    return lv_color_hex(!strcmp(s,"waiting") ? 0xffd478 : !strcmp(s,"completed") ? 0x9fdda9 :
                        !strcmp(s,"systemError") ? 0xff9999 : !strcmp(s,"active") ? 0x81d7ee :
                        !strcmp(s,"idle") ? 0x9aa7b2 : 0xc6a8ef);
}
static double age(void) { return received_age + (esp_timer_get_time()-received_us)/1000000.; }
static bool stale(void) { return !snapshot || age() > 30; }
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int w, int h) {
    lv_obj_t *o=lv_label_create(parent);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_label_set_text(o,text);
    return o;
}
static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                        lv_event_cb_t cb, intptr_t value) {
    lv_obj_t *o=lv_button_create(parent);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x172630),0);
    lv_obj_set_style_radius(o,9,0); lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,(void *)value);
    if (text && *text) { lv_obj_t *l=lv_label_create(o); lv_label_set_text(l,text); lv_obj_center(l); }
    return o;
}
static void rebuild(void);
static void refresh(void);
static void change_page(lv_event_t *e) {
    page=(int)(intptr_t)lv_event_get_user_data(e);
    selected_id[0]=0; choosing=false; rebuild();
    ESP_LOGI("panel","PAGE %d",page);
}
static void move_list(lv_event_t *e) { list_page+=(int)(intptr_t)lv_event_get_user_data(e); refresh(); }
static void open_task(lv_event_t *e) {
    cJSON *task=cJSON_GetArrayItem(tasks(),list_page*2+(int)(intptr_t)lv_event_get_user_data(e));
    if (!task) return;
    snprintf(selected_id,sizeof(selected_id),"%s",str(task,"id"));
    snprintf(selected_source,sizeof(selected_source),"%s",str(task,"source"));
    rebuild(); ESP_LOGI("panel","DETAIL opened");
}
static void back(lv_event_t *e) { (void)e; selected_id[0]=0; choosing=false; rebuild(); }
static void choose_provider(lv_event_t *e) {
    (void)e; choosing=true; rebuild();
}
static void select_provider(lv_event_t *e) {
    provider_index=(int)(intptr_t)lv_event_get_user_data(e); window_index=0;
    choosing=false; rebuild();
}
static void move_window(lv_event_t *e) {
    window_index+=(int)(intptr_t)lv_event_get_user_data(e); refresh();
}
static void refresh(void) {
    char text[320];
    if (!configured) snprintf(text,sizeof(text),"请配置 Wi-Fi");
    else if (offline) snprintf(text,sizeof(text),"离线");
    else if (!wifi_ok || !http_ok || !snapshot) snprintf(text,sizeof(text),"同步中");
    else snprintf(text,sizeof(text),"%s",stale() ? "数据已过期" : "已同步");
    lv_label_set_text(connection_label,text);
    lv_obj_set_style_text_color(connection_label,lv_color_hex(offline||stale()?0xffd478:0xacc3ce),0);
    if (choosing) return;
    if (*selected_id) {
        cJSON *task=NULL,*entry;
        cJSON_ArrayForEach(entry,tasks()) if (!strcmp(str(entry,"id"),selected_id) && !strcmp(str(entry,"source"),selected_source)) { task=entry; break; }
        lv_label_set_text(detail_title,task ? str(task,"title") : "会话已不在当前列表中");
        snprintf(text,sizeof(text),"%s · %s%s",source_name(selected_source),stale()?"上次：":"",task?state_name(str(task,"status")):"不可用");
        lv_label_set_text(detail_state,text);
        lv_obj_set_style_text_color(detail_state,task?state_color(str(task,"status")):lv_color_hex(0xacc3ce),0);
        return;
    }
    if (!page) {
        int count=cJSON_GetArraySize(tasks()), pages=(count+1)/2;
        if (!pages) pages=1;
        if (list_page>=pages) list_page=pages-1;
        if (list_page<0) list_page=0;
        for (int i=0;i<2;i++) {
            cJSON *task=cJSON_GetArrayItem(tasks(),list_page*2+i);
            lv_obj_remove_flag(rows[i],LV_OBJ_FLAG_HIDDEN);
            if (!task && i) { lv_obj_add_flag(rows[i],LV_OBJ_FLAG_HIDDEN); continue; }
            lv_label_set_text(titles[i],task ? str(task,"title") : snapshot ? "当前没有会话" : "等待会话数据");
            snprintf(text,sizeof(text),"%s    %s%s",task?source_name(str(task,"source")):"",stale()&&task?"上次：":"",task?state_name(str(task,"status")):"请启动电脑端服务");
            lv_label_set_text(states[i],text);
            lv_obj_set_style_text_color(states[i],task?state_color(str(task,"status")):lv_color_hex(0xacc3ce),0);
        }
        snprintf(text,sizeof(text),"%d / %d",list_page+1,pages); lv_label_set_text(pager,text);
        lv_obj_set_state(previous,LV_STATE_DISABLED,list_page==0);
        lv_obj_set_state(next,LV_STATE_DISABLED,list_page==pages-1);
    } else {
        cJSON *provider=cJSON_GetArrayItem(providers(),provider_index);
        cJSON *windows=cJSON_GetObjectItemCaseSensitive(provider,"windows");
        int count=cJSON_GetArraySize(windows);
        if(window_index<0) window_index=0;
        if(window_index>=count) window_index=count?count-1:0;
        cJSON *window=cJSON_GetArrayItem(windows,window_index);
        if (!window) {
            lv_label_set_text(quota_caption,provider_index==0?"Codex":provider_index==1?"Kimi":"DSH");
            lv_label_set_text(quota_value,"--");
            lv_label_set_text(quota_note,provider_index==0?"暂无额度数据":"额度暂未接入");
            lv_bar_set_value(quota_bar,0,LV_ANIM_OFF);
            lv_label_set_text(quota_bucket,"");
        } else {
            double minutes=num(window,"minutes",0);
            if(minutes>=1440 && fmod(minutes,1440)==0) snprintf(text,sizeof(text),"%s · %.0f 天额度",source_name(str(provider,"id")),minutes/1440);
            else if(minutes>=60 && fmod(minutes,60)==0) snprintf(text,sizeof(text),"%s · %.0f 小时额度",source_name(str(provider,"id")),minutes/60);
            else if(minutes>0) snprintf(text,sizeof(text),"%s · %.0f 分钟额度",source_name(str(provider,"id")),minutes);
            else snprintf(text,sizeof(text),"%s · 周期未知",source_name(str(provider,"id")));
            lv_label_set_text(quota_caption,text);
            lv_label_set_text(quota_bucket,str(window,"name"));
            snprintf(text,sizeof(text),"%.0f%%",num(window,"left",0)); lv_label_set_text(quota_value,text);
            lv_bar_set_value(quota_bar,(int)num(window,"left",0),LV_ANIM_OFF);
            double reset=num(window,"resets_at",0);
            int seconds=(int)fmax(0,reset-server_time-(esp_timer_get_time()-received_us)/1000000.);
            if(stale()) snprintf(text,sizeof(text),"旧数据，仅供参考");
            else if(!reset) snprintf(text,sizeof(text),"重置时间未知");
            else if(seconds>=86400) snprintf(text,sizeof(text),"%d 天 %d 小时后重置",seconds/86400,seconds%86400/3600);
            else snprintf(text,sizeof(text),"%d 小时 %d 分钟后重置",seconds/3600,seconds%3600/60);
            lv_label_set_text(quota_note,text);
        }
        snprintf(text,sizeof(text),"%d / %d",count?window_index+1:0,count); lv_label_set_text(window_label,text);
        lv_obj_set_state(previous,LV_STATE_DISABLED,window_index==0);
        lv_obj_set_state(next,LV_STATE_DISABLED,!count || window_index>=count-1);
    }
}
static void rebuild(void) {
    lv_obj_clean(header); lv_obj_clean(body);
    lv_obj_set_scroll_dir(body,LV_DIR_NONE);
    connection_label=label(header,"",278,20,306,40);
    lv_obj_set_style_text_align(connection_label,LV_TEXT_ALIGN_RIGHT,0);
    for(int i=0;i<2;i++) lv_obj_set_style_bg_color(nav[i],lv_color_hex(i==page?0x244957:0x172630),0);
    if (*selected_id || choosing) button(header,"< 返回",16,12,160,48,back,0);
    else if(page) {
        lv_obj_t *selector=button(header,provider_index==0?"Codex · 切换":provider_index==1?"Kimi · 切换":"DSH · 切换",16,12,244,48,choose_provider,0);
        lv_obj_set_style_bg_color(selector,lv_color_hex(0x244957),0);
        lv_obj_set_style_border_width(selector,2,0);
        lv_obj_set_style_border_color(selector,lv_color_hex(0x81d7ee),0);
        lv_obj_set_style_bg_color(selector,lv_color_hex(0x39758a),LV_STATE_PRESSED);
    }
    else label(header,"会话",16,20,240,40);
    if (choosing) {
        const char *names[]={"Codex","Kimi","DSH"};
        for(int i=0;i<3;i++) button(body,names[i],12,8+i*100,552,88,select_provider,i);
    } else if (*selected_id) {
        lv_obj_set_scroll_dir(body,LV_DIR_VER);
        detail_state=label(body,"",14,8,550,40);
        detail_title=label(body,"",14,58,548,LV_SIZE_CONTENT);
        lv_label_set_long_mode(detail_title,LV_LABEL_LONG_MODE_WRAP);
    } else if (!page) {
        for(int i=0;i<2;i++) {
            rows[i]=button(body,"",12,i*132,552,124,open_task,i);
            lv_obj_set_style_pad_all(rows[i],0,0);
            titles[i]=label(rows[i],"",14,4,524,72);
            lv_label_set_long_mode(titles[i],LV_LABEL_LONG_MODE_DOTS);
            states[i]=label(rows[i],"",14,82,524,38);
        }
        previous=button(body,"< 上页",12,266,168,52,move_list,-1);
        pager=label(body,"",225,274,125,40); lv_obj_set_style_text_align(pager,LV_TEXT_ALIGN_CENTER,0);
        next=button(body,"下页 >",396,266,168,52,move_list,1);
    } else {
        quota_caption=label(body,"",16,8,550,40);
        quota_value=label(body,"--",16,65,550,68);
        lv_obj_set_style_text_font(quota_value,&lv_font_montserrat_48,0);
        quota_bar=lv_bar_create(body); lv_obj_set_pos(quota_bar,16,153); lv_obj_set_size(quota_bar,544,12);
        lv_obj_set_style_bg_color(quota_bar,lv_color_hex(0x81d7ee),LV_PART_INDICATOR);
        quota_note=label(body,"",16,178,550,40);
        quota_bucket=label(body,"",16,224,550,36);
        lv_label_set_long_mode(quota_bucket,LV_LABEL_LONG_MODE_DOTS);
        previous=button(body,"< 周期",12,266,168,52,move_window,-1);
        window_label=label(body,"",225,274,125,40); lv_obj_set_style_text_align(window_label,LV_TEXT_ALIGN_CENTER,0);
        next=button(body,"周期 >",396,266,168,52,move_window,1);
    }
    refresh();
}
void panel_network(bool wifi, bool http, bool ready, bool is_offline) {
    wifi_ok=wifi; http_ok=http; configured=ready; offline=is_offline; refresh();
}
static bool valid_string(cJSON *o,const char *key,size_t max) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);
    return cJSON_IsString(v) && v->valuestring && strlen(v->valuestring)<=max;
}
bool panel_accept(const char *json) {
    bool pressed=false;
    for(lv_indev_t *input=lv_indev_get_next(NULL);input;input=lv_indev_get_next(input))
        if(lv_indev_get_state(input)==LV_INDEV_STATE_PRESSED) pressed=true;
    cJSON *incoming=cJSON_Parse(json), *t=cJSON_GetObjectItemCaseSensitive(incoming,"tasks"), *p=cJSON_GetObjectItemCaseSensitive(incoming,"providers"), *entry;
    bool valid=incoming && num(incoming,"version",0)==1 && cJSON_IsArray(t) && cJSON_GetArraySize(t)<=8 &&
        cJSON_IsArray(p) && cJSON_GetArraySize(p)==3 && num(incoming,"age_seconds",-1)>=0 && num(incoming,"server_time",-1)>=0;
    cJSON_ArrayForEach(entry,t) {
        valid=valid && valid_string(entry,"id",255) && *str(entry,"id") && valid_string(entry,"title",4096) &&
            valid_string(entry,"status",32) && valid_string(entry,"source",15);
        const char *source=str(entry,"source");
        valid=valid && (!strcmp(source,"codex") || !strcmp(source,"kimi") || !strcmp(source,"dsh"));
    }
    const char *expected[]={"codex","kimi","dsh"};
    for(int i=0;i<3;i++) valid=valid && !strcmp(str(cJSON_GetArrayItem(p,i),"id"),expected[i]);
    cJSON_ArrayForEach(entry,p) {
        cJSON *windows=cJSON_GetObjectItemCaseSensitive(entry,"windows"),*w;
        valid=valid && valid_string(entry,"id",15) && cJSON_IsArray(windows) && cJSON_GetArraySize(windows)<=16;
        cJSON_ArrayForEach(w,windows) valid=valid && num(w,"left",-1)>=0 && num(w,"left",101)<=100;
    }
    if(!valid) { cJSON_Delete(incoming); return false; }
    // Deferring a valid snapshot during a touch is not a network failure.
    if(pressed) { cJSON_Delete(incoming); return true; }
    // Adopt the server's latest activity order; the pressed-input guard above
    // keeps a row from moving underneath a touch in progress.
    cJSON_Delete(snapshot); snapshot=incoming;
    received_us=esp_timer_get_time(); received_age=num(snapshot,"age_seconds",0); server_time=num(snapshot,"server_time",0);
    refresh(); return true;
}
static void tick(lv_timer_t *timer) { (void)timer; refresh(); }
void panel_debug_page(int target) {
    if(target<0 || target>3) return;
    page=target==1 || target==3; choosing=false; selected_id[0]=0;
    if(target==1 || target==3) { provider_index=target==3?1:0; window_index=0; }
    if(target==2) {
        cJSON *task=cJSON_GetArrayItem(tasks(),list_page*2);
        if(task) { snprintf(selected_id,sizeof(selected_id),"%s",str(task,"id")); snprintf(selected_source,sizeof(selected_source),"%s",str(task,"source")); }
    }
    rebuild();
}
void panel_init(void) {
    lv_obj_t *screen=lv_screen_active();
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x080e13),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(0xeff5f7),0);
    lv_obj_set_style_text_font(screen,&font_cjk_28,0);
    header=lv_obj_create(screen); body=lv_obj_create(screen);
    lv_obj_t *containers[]={header,body};
    for(int i=0;i<2;i++) { lv_obj_remove_style_all(containers[i]); lv_obj_set_size(containers[i],600,i?322:64); }
    lv_obj_set_pos(body,12,64); lv_obj_set_width(body,576);
    nav[0]=button(screen,"会话",0,390,298,60,change_page,0);
    nav[1]=button(screen,"额度",302,390,298,60,change_page,1);
    rebuild(); lv_timer_create(tick,1000,NULL);
}
