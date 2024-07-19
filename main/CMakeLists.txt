idf_component_register(
 SRCS
  "acr.cc"
  "app.cc"
  "app_support.cc"
  "button_driver.cc"
  "http_ui.cc"
  "main.cc"
  "resource.cc"
  "rgb_led.cc"
  "telnet.cc"
  "wifi_manager.cc"
 INCLUDE_DIRS
   "."
 EMBED_TXTFILES
   "data/global.css"
   "data/index.html"
   "data/test.html"
 EMBED_FILES
   "data/test.png"
 )

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-missing-field-initializers")
