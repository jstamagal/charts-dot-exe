#ifndef CHARTS_APP_PRINTER_H
#define CHARTS_APP_PRINTER_H
#include "charts.h"
#include "model.h"
int app_printer_image(lc_context *ctx,const lc_image *image,const char *target,
                      const char *preview,app_error *err);
int app_printer_status(const char *target,app_error *err);
int app_printer_serve(const char *address,app_error *err);
#endif
