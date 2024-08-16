

// Reminder:
//  - the first argument is the internal name of the resource. 
//  - the second argument is the external base label that is typically
//    created by EMBED_TXTFILES or EMBED_FILES from CMakeList.txt.
//    This is the basename of the embedded file where
//    all 'non-label' characters are replaced by '_'.
//    For example, 'data/xxx.yyy' becomes 'xxx_yyy' 
//

// See also resource.c

#define RESOURCES \
  DEF_TEXT("/data/jquery.3.7.1.js", jquery_3_7_1_js, "text/javascript") \
  DEF_TEXT("/data/global.css", global_css, "text/plain") \
  DEF_BINARY("/data/icon.png", icon_png, "image/png") \
  DEF_TEXT("/index.html", index_html, "text/html") \
  DEF_TEXT("/page/help.html", help_html, "text/html") \

/////////////////////////
