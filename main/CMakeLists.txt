idf_component_register(
 SRCS
  "acr.cc"
  "app.cc"
  "app_support.cc"
  "button_driver.cc"
  "resource.cc"
  "rgb_led.cc"
  "ui_http.cc"
  "ui_mqtt.cc"
 INCLUDE_DIRS
   "."
 EMBED_TXTFILES
   "data/global.css"
   "data/index.html"
   "data/help.html"
   "data/jquery.3.7.1.js"
 EMBED_FILES
   "data/icon.png"
 )

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-missing-field-initializers")
