/* Compiled as C, to check that the header is valid C. */
#include "ogccdb/runtime/ogccdb_runtime.h"

int ogccdb_rt_c_smoke(void) {
    ogccdb_rt_options o;
    double p[3];
    double lat = 0.0, lon = 0.0, h = 0.0;
    ogccdb_rt_default_options(&o);
    if (o.mesh_resolution <= 0) return 1;
    if (ogccdb_rt_open(0, &o) != 0) return 2;
    if (ogccdb_rt_last_error()[0] == '\0') return 3;
    ogccdb_rt_geodetic_to_ecef(47.5, 8.5, 1000.0, p);
    ogccdb_rt_ecef_to_geodetic(p, &lat, &lon, &h);
    if (lat < 47.4999 || lat > 47.5001 || h < 999.99 || h > 1000.01) return 4;
    return 0;
}
