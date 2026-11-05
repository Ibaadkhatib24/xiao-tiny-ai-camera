#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "img_converters.h"
#include <LittleFS.h>
#undef HTTP_ANY
#include "esp_http_server.h"
#include <math.h>

WebServer server(80);
bool cameraReady = false;
const char *ssid = "XIAO-Camera";
const char *password = "Camera7392!";
constexpr int FEATURES=192, CLASSES=3, CAPACITY=30;
uint8_t samples[CLASSES][CAPACITY][FEATURES] = {};
uint8_t counts[CLASSES] = {};
uint8_t *rgb=nullptr;
SemaphoreHandle_t cameraMutex;
bool storageReady=false;
bool saveModel() {
  if(!storageReady) return false;
  File f=LittleFS.open("/model.tmp","w");
  if(!f) return false;
  bool ok=f.write(counts,sizeof(counts))==sizeof(counts);
  ok=(f.write((uint8_t*)samples,sizeof(samples))==sizeof(samples)) && ok;
  f.close();
  if(!ok) {LittleFS.remove("/model.tmp");return false;}
  return LittleFS.rename("/model.tmp","/model.bin");
}

bool features(uint8_t *out) {
  if(!cameraReady || !rgb || xSemaphoreTake(cameraMutex,pdMS_TO_TICKS(2000))!=pdTRUE) return false;
  camera_fb_t *fb=esp_camera_fb_get();
  bool ok=fb && fb->width==320 && fb->height==240 && fmt2rgb888(fb->buf,fb->len,fb->format,rgb);
  if(fb) esp_camera_fb_return(fb);
  if(ok) {
    for(int by=0;by<8;by++) for(int bx=0;bx<8;bx++) {
      uint32_t sums[3]={};
      for(int y=by*30;y<(by+1)*30;y+=3) for(int x=bx*40;x<(bx+1)*40;x+=4)
        for(int c=0;c<3;c++) sums[c]+=rgb[(y*320+x)*3+c];
      for(int c=0;c<3;c++) out[(by*8+bx)*3+c]=sums[c]/100;
    }
  }
  xSemaphoreGive(cameraMutex);
  return ok;
}
float distanceTo(const uint8_t *a,const uint8_t *b) {
  float sum=0;
  for(int i=0;i<FEATURES;i++) {float d=float(a[i])-b[i];sum+=d*d;}
  return sqrtf(sum/FEATURES);
}
String state() {
  return String("{\"counts\":[")+counts[0]+","+counts[1]+","+counts[2]+"]}";
}
void learn() {
  String value=server.arg("class");
  if(value!="0" && value!="1" && value!="2") {server.send(400,"text/plain","Invalid class");return;}
  int c=value.toInt();
  if(counts[c]>=CAPACITY) {server.send(409,"text/plain","Class full. Reset it to retrain.");return;}
  uint8_t f[FEATURES];
  if(!features(f)) {server.send(503,"text/plain","Camera capture failed");return;}
  memcpy(samples[c][counts[c]++],f,FEATURES);
  if(!saveModel()) {counts[c]--;server.send(500,"text/plain","Could not save example");return;}
  server.send(200,"application/json",state());
}
void predict() {
  int active=0;for(int c=0;c<CLASSES;c++) if(counts[c]>=5) active++;
  if(active<2) {server.send(200,"application/json","{\"ready\":false}");return;}
  uint8_t f[FEATURES]; unsigned long start=millis();
  if(!features(f)) {server.send(503,"text/plain","Camera capture failed");return;}
  float distances[3]={1000,1000,1000};int best=-1;
  for(int c=0;c<CLASSES;c++) if(counts[c]>=5) {
    float nearest[3]={1000,1000,1000};
    for(int n=0;n<counts[c];n++) {
      float d=distanceTo(f,samples[c][n]);
      for(int k=0;k<3;k++) if(d<nearest[k]) {for(int j=2;j>k;j--) nearest[j]=nearest[j-1];nearest[k]=d;break;}
    }
    distances[c]=(nearest[0]+nearest[1]+nearest[2])/3;
    if(best<0 || distances[c]<distances[best]) best=c;
  }
  float second=1000;for(int c=0;c<3;c++) if(c!=best && distances[c]<second) second=distances[c];
  float margin=(second-distances[best])/fmaxf(second,1);
  bool uncertain=margin<0.12f || distances[best]>55;
  String body=String("{\"ready\":true,\"class\":")+best+",\"uncertain\":"+(uncertain?"true":"false")+",\"margin\":"+String(margin,3)+",\"ms\":"+String(millis()-start)+"}";
  server.send(200,"application/json",body);
}
esp_err_t stream(httpd_req_t *req) {
  httpd_resp_set_type(req,"multipart/x-mixed-replace;boundary=frame");
  httpd_resp_set_hdr(req,"Cache-Control","no-store");
  while(cameraReady) {
    if(xSemaphoreTake(cameraMutex,pdMS_TO_TICKS(2000))!=pdTRUE) break;
    camera_fb_t *fb=esp_camera_fb_get();
    esp_err_t res=ESP_FAIL;
    if(fb) {
      char header[100];int n=snprintf(header,sizeof(header),"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",(unsigned)fb->len);
      res=httpd_resp_send_chunk(req,header,n);
      if(res==ESP_OK) res=httpd_resp_send_chunk(req,(const char*)fb->buf,fb->len);
      if(res==ESP_OK) res=httpd_resp_send_chunk(req,"\r\n",2);
      esp_camera_fb_return(fb);
    }
    xSemaphoreGive(cameraMutex);
    if(res!=ESP_OK) break;
    vTaskDelay(pdMS_TO_TICKS(70));
  }
  return ESP_OK;
}
const char page[] PROGMEM = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>Tiny Camera AI</title>
<style>body{background:#0e1724;color:#eef5ff;font:16px system-ui;margin:0;padding:22px}main{max-width:780px;margin:auto}h1{margin-bottom:5px}p,small{color:#aebdd1;line-height:1.5}img{width:100%;max-height:480px;object-fit:contain;background:#050a12;border-radius:14px}#prediction{font-size:26px;margin:16px 0 4px}.card{background:#1b283b;border-radius:12px;padding:14px;margin:10px 0;display:flex;gap:10px;flex-wrap:wrap;align-items:center}input{min-width:100px;width:130px;background:#101a29;color:white;border:1px solid #536479;padding:10px;border-radius:6px}button{background:#9cebc9;color:#102e23;border:0;border-radius:8px;padding:11px;cursor:pointer}button:disabled{opacity:.5}small{display:block}#message{min-height:24px;color:#9cebc9}</style></head><body><main>
<h1>Tiny Camera AI</h1><p>Teach your board two or three different objects or scenes. Keep the background steady and fill the view with each object.</p>
<img id="view" alt="Live camera preview"><button id="pause">Pause preview</button><div id="prediction">Collect examples to begin</div><small id="details">Predictions run on the board.</small>
<div id="classes"></div><p id="message"></p><small>Collect at least 5 examples per class; 15–30 works better. Move the object slightly between examples. Test with new poses after training. “Separation” compares the closest classes; it is not a probability of correctness.</small><p>This small model learns colors and coarse layout, not general object meaning. Similar backgrounds can confuse it. Examples stay on the board across restarts; names are saved in this browser.</p></main>
<script>
const $=s=>document.querySelector(s);let names;try{names=JSON.parse(localStorage.getItem('tiny-names'))}catch(e){}if(!Array.isArray(names)||names.length!==3)names=['Object A','Object B','Empty scene'];let counts=[0,0,0],busy=false,paused=false;
const image=$('#view');function start(){image.src='http://'+location.hostname+':81/stream?t='+Date.now()}start();$('#pause').onclick=()=>{paused=!paused;$('#pause').textContent=paused?'Resume preview':'Pause preview';if(paused)image.removeAttribute('src');else start()};image.onerror=()=>{$('#message').textContent='Preview disconnected. Tap Resume to reconnect.'};
function refresh(){counts.forEach((n,i)=>{$('#count'+i).textContent=n+'/30 examples'})}
for(let i=0;i<3;i++){let card=document.createElement('div');card.className='card';let input=document.createElement('input');input.maxLength=24;input.value=names[i];input.setAttribute('aria-label','Class '+(i+1)+' name');input.onchange=()=>{names[i]=input.value||'Class '+(i+1);localStorage.setItem('tiny-names',JSON.stringify(names))};card.append(input);let count=document.createElement('span');count.id='count'+i;card.append(count);let add=document.createElement('button');add.textContent='Capture 5 examples';add.onclick=()=>collect(i);card.append(add);let reset=document.createElement('button');reset.textContent='Reset';reset.onclick=()=>resetClass(i);card.append(reset);$('#classes').append(card)}refresh();
async function api(path,body){let r=await fetch(path,{method:body===undefined?'GET':'POST',headers:body===undefined?{}:{'Content-Type':'application/x-www-form-urlencoded'},body,signal:AbortSignal.timeout(10000)});if(!r.ok)throw Error(await r.text());return r.json()}
function lock(v){busy=v;document.querySelectorAll('.card button').forEach(b=>b.disabled=v)}
async function collect(i){if(busy)return;lock(true);try{for(let n=0;n<5 && counts[i]<30;n++){$('#message').textContent='Capturing '+names[i]+' — move it slightly…';let s=await api('/learn','class='+i);counts=s.counts;refresh();await new Promise(r=>setTimeout(r,450))}$('#message').textContent='Examples saved.'}catch(e){$('#message').textContent=e.message}finally{lock(false)}}
async function resetClass(i){if(busy || !confirm('Delete examples for '+names[i]+'?'))return;lock(true);try{counts=(await api('/reset','class='+i)).counts;refresh()}catch(e){$('#message').textContent=e.message}finally{lock(false)}}
async function tick(){if(!busy){try{let p=await api('/predict');$('#prediction').textContent=!p.ready?'Train at least two classes':p.uncertain?'Unsure — closest: '+names[p.class]:names[p.class];$('#details').textContent=p.ready?'Separation: '+Math.round(p.margin*100)+'% · Capture + prediction: '+p.ms+' ms':'At least 5 examples in each of two classes.'}catch(e){$('#details').textContent='Connection interrupted: '+e.message}}setTimeout(tick,1300)}
api('/state').then(s=>{counts=s.counts;refresh();tick()}).catch(e=>{$('#message').textContent=e.message;tick()});
</script></body></html>)HTML";

void setup() {
  Serial.begin(115200);
  delay(1500);
  pinMode(LED_BUILTIN,OUTPUT);
  digitalWrite(LED_BUILTIN,HIGH);
  camera_config_t c = {};
  c.ledc_channel=LEDC_CHANNEL_0; c.ledc_timer=LEDC_TIMER_0;
  c.pin_d0=15; c.pin_d1=17; c.pin_d2=18; c.pin_d3=16;
  c.pin_d4=14; c.pin_d5=12; c.pin_d6=11; c.pin_d7=48;
  c.pin_xclk=10; c.pin_pclk=13; c.pin_vsync=38; c.pin_href=47;
  c.pin_sccb_sda=40; c.pin_sccb_scl=39; c.pin_pwdn=-1; c.pin_reset=-1;
  c.xclk_freq_hz=20000000; c.pixel_format=PIXFORMAT_JPEG;
  c.frame_size=FRAMESIZE_QVGA; c.jpeg_quality=16;
  c.fb_count=psramFound()?2:1;
  c.fb_location=psramFound()?CAMERA_FB_IN_PSRAM:CAMERA_FB_IN_DRAM;
  c.grab_mode=CAMERA_GRAB_LATEST;
  esp_err_t err=esp_camera_init(&c);
  cameraReady=err==ESP_OK;
  Serial.printf("Camera init: 0x%x; PSRAM: %u bytes\n",err,(unsigned)ESP.getPsramSize());
  if(cameraReady) {
    auto fb=esp_camera_fb_get();
    Serial.printf("Camera self-test: %s, JPEG bytes: %u\n",fb?"PASS":"FAIL",fb?(unsigned)fb->len:0);
    if(fb) esp_camera_fb_return(fb);
  }
  cameraMutex=xSemaphoreCreateMutex();
  rgb=(uint8_t*)ps_malloc(320*240*3);
  storageReady=LittleFS.begin(true);
  if(storageReady) {
    File f=LittleFS.open("/model.bin","r");
    if(f && f.size()==sizeof(counts)+sizeof(samples)) {
      f.read(counts,sizeof(counts));f.read((uint8_t*)samples,sizeof(samples));
      for(int i=0;i<3;i++) if(counts[i]>CAPACITY) counts[i]=0;
    }
    if(f) f.close();
    File check=LittleFS.open("/storage-test","w");
    bool ok=check && check.write((const uint8_t*)"ok",2)==2;check.close();
    File verify=LittleFS.open("/storage-test","r");ok=ok && verify && verify.readString()=="ok";verify.close();LittleFS.remove("/storage-test");
    Serial.printf("Persistent storage self-test: %s\n",ok?"PASS":"FAIL");
  }
  uint8_t test[FEATURES];
  Serial.printf("Feature extraction self-test: %s\n",features(test)?"PASS":"FAIL");
  uint8_t a[FEATURES]={},b[FEATURES];memset(b,100,sizeof(b));
  Serial.printf("Classifier distance self-test: %s\n",distanceTo(a,a)==0 && fabsf(distanceTo(a,b)-100)<0.01f?"PASS":"FAIL");
  WiFi.mode(WIFI_AP);
  if(!WiFi.softAP(ssid,password)) { Serial.println("Wi-Fi AP failed"); return; }
  server.on("/",[]{server.send_P(200,"text/html; charset=utf-8",page);});

  server.on("/learn",HTTP_POST,learn);
  server.on("/predict",HTTP_GET,predict);
  server.on("/state",HTTP_GET,[]{server.send(200,"application/json",state());});
  server.on("/reset",HTTP_POST,[]{
    String value=server.arg("class");
    if(value!="0" && value!="1" && value!="2") {server.send(400,"text/plain","Invalid class");return;}
    int c=value.toInt();uint8_t old=counts[c];counts[c]=0;
    if(!saveModel()) {counts[c]=old;server.send(500,"text/plain","Could not save reset");return;}
    server.send(200,"application/json",state());
  });
  server.on("/health",[]{server.send(cameraReady?200:503,"text/plain",cameraReady?"Camera ready":"Camera unavailable");});
  server.begin();
  httpd_config_t cfg=HTTPD_DEFAULT_CONFIG();cfg.server_port=81;cfg.ctrl_port=32769;cfg.send_wait_timeout=2;
  httpd_handle_t handle=nullptr;
  if(httpd_start(&handle,&cfg)==ESP_OK) {
    httpd_uri_t uri={};uri.uri="/stream";uri.method=HTTP_GET;uri.handler=stream;
    httpd_register_uri_handler(handle,&uri);Serial.println("MJPEG stream ready on port 81");
  }
  Serial.printf("Wi-Fi: %s\nViewer: http://%s\n",ssid,WiFi.softAPIP().toString().c_str());
  digitalWrite(LED_BUILTIN,cameraReady?LOW:HIGH);
}
void loop() {
  server.handleClient();
  static unsigned long last=0;
  if(millis()-last>10000) {
    last=millis();
    Serial.printf("Camera: %s; connected viewers: %u\n",cameraReady?"ready":"unavailable",WiFi.softAPgetStationNum());
  }
  delay(2);
}
