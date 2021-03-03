/*
 * MapGIS 6.7 file utility
 * 目前是将 .WP 文件转成 GeoJSON 文件
 */

#include <stdio.h>  // printf()
#include <sys/types.h>  // open()
#include <sys/stat.h>  // open()
#include <fcntl.h>  // open()
#include <err.h>  // err()
#include <unistd.h>  // read()
#include <stdlib.h>  // malloc() and free()
#include <iconv.h>  // iconv_open(), iconv()
#include <string.h>  // strncpy()
#include <strings.h>  // bzero()
#include <math.h>  // round()

#include "cJSON.h"
#include "mapgisf.h"

#define MAPGIS_UTIL_DEBUG

#ifdef MAPGIS_UTIL_DEBUG
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

int g_num_line = 0; // 总线数，主e要用于判断线号越界

// 还有一堆懒得写在这里了
static void print_fh(struct file_header *fh);
static void print_dh(struct data_header *dh);
static void print_dhs(int file_type, struct data_headers *dhs);
static void print_polygon_info(struct polygon_info *pi, void *line_coords, size_t line_coords_len);
static void print_polygon_infos(int n, struct polygon_info *pis, void *line_coords, size_t line_coords_len);

static void
print_fh(struct file_header *fh) {
    int type_id = fh->ftype_id;

    if (type_id < 0 || type_id > 2) {
        err(1, "Invalid file type %d", type_id);
    }
    DEBUG_PRINT("文件头信息\n");
    DEBUG_PRINT("==========\n");
    DEBUG_PRINT("Type: %s\n", file_type_names[type_id]);
    DEBUG_PRINT("data headers 偏移量: %d, guess_num_data_headers: %d\n", fh->off_data_headers, fh->guess_num_data_headers);
    DEBUG_PRINT("线数: %d(%d), 点数: %d(%d), 多边形数: %d(%d)\n", fh->num_lines, fh->num_lines_pad, fh->num_points, fh->num_points_pad,
            fh->num_polygons, fh->num_polygons_pad);
    DEBUG_PRINT("xmin: %f, ymin: %f, xmax: %f, ymax: %f\n", fh->xmin, fh->ymin, fh->xmax, fh->ymax);
    DEBUG_PRINT("\n");
}

static void
print_dh(struct data_header *dh) {
    DEBUG_PRINT("起始:%d:(0x%x):结束:%d:(0x%x):大小:%d(0x%x)\n", dh->data_offset, dh->data_offset,
            dh->data_offset + dh->data_len - 1, dh->data_offset + dh->data_len - 1, dh->data_len, dh->data_len);
}

static void
print_dhs(int ft /* file type */, struct data_headers *dhs) {
    DEBUG_PRINT("数据区头信息，相当于一个目录\n");
    DEBUG_PRINT("============================\n");
    if (ft == MAPGIS_F_TYPE_POINT) {
        DEBUG_PRINT("点信息(点文件的): ");
    } else {
        DEBUG_PRINT("线信息(面、线文件的): ");
    }
    print_dh(&dhs->line_or_point_info);

    if (ft == MAPGIS_F_TYPE_POINT) {
        DEBUG_PRINT("点字符串(点文件的): ");
    } else {
        DEBUG_PRINT("线坐标点(面、线文件的): ");
    }
    print_dh(&dhs->line_coords_or_point_string);

    if (ft == MAPGIS_F_TYPE_POINT) {
        DEBUG_PRINT("点属性(点文件的): ");
    } else {
        DEBUG_PRINT("线属性(面、线文件的): ");
    }
    print_dh(&dhs->line_or_point_attr);

    DEBUG_PRINT("线拓扑关系: ");
    print_dh(&dhs->line_topo_relation);

    DEBUG_PRINT("结点信息: ");
    print_dh(&dhs->node_info);

    DEBUG_PRINT("结点属性: ");
    print_dh(&dhs->node_attr);

    DEBUG_PRINT("unknown信息: ");
    print_dh(&dhs->unknown_info);

    DEBUG_PRINT("unknown属性: ");
    print_dh(&dhs->unknown_attr);

    DEBUG_PRINT("多边形信息: ");
    print_dh(&dhs->polygon_info);

    DEBUG_PRINT("多边形属性: ");
    print_dh(&dhs->polygon_attr);

    DEBUG_PRINT("pad属性: ");
    print_dh(&dhs->pad);

    DEBUG_PRINT("pad1属性: ");
    print_dh(&dhs->pad1);

    DEBUG_PRINT("pad12属性: ");
    print_dh(&dhs->pad12);

    DEBUG_PRINT("pad13属性: ");
    print_dh(&dhs->pad13);

    DEBUG_PRINT("pad14属性: ");
    print_dh(&dhs->pad14);

    DEBUG_PRINT("pad15属性: ");
    print_dh(&dhs->pad15);
    DEBUG_PRINT("\n");
}

/*
 * 打印单个 polygon 的信息
 */
static void
print_polygon_info(struct polygon_info *pi, void *line_coords, size_t line_coords_len) {
    int *line_num;  // 线号

    DEBUG_PRINT("polygon 信息\n");
    DEBUG_PRINT("============\n");
    DEBUG_PRINT("flag=%d, 线总数=%d, 线号存储位置=%d, 颜色=%d, 填充图案号=%d, 图案高=%f, 图案宽=%f\n", pi->flag, pi->num_lines, pi->off_line_info, pi->color,
            pi->fill_pattern_index, pi->pattern_height, pi->pattern_width);
    DEBUG_PRINT("笔宽=%d, 图案颜色=%d, 透明输出=%d, 图层=%d, 线号1=%d, 线号2=%d\n", pi->pen_width, pi->pattern_color, pi->transparent_output, pi->layer,
            pi->line_index1, pi->line_index2);
    if (pi->off_line_info >= line_coords_len) {
        err(1, "线号信息超出范围");
    }
    line_num = (int *)(line_coords + pi->off_line_info);
    for (int i = 0; i < pi->num_lines; i++) {
        DEBUG_PRINT("线号 %d: %d(0x%x)", i, *line_num, *line_num);
        if (*line_num > g_num_line) {
            DEBUG_PRINT(" 越界，最大 %d\n", g_num_line);
        } else {
            DEBUG_PRINT("\n");
        }
        line_num++;
    }
    DEBUG_PRINT("\n");
};

/*
 * 打印每个 polygon 的信息
 */
static void
print_polygon_infos(int n, struct polygon_info *pis, void *line_coords, size_t line_coords_len) {
    struct polygon_info *pi = pis;

    for (int i = 0; i <= n; i++) {  // 故意这么写的，多了一次循环
        DEBUG_PRINT("Polygon %d:\n", i);
        print_polygon_info(pi, line_coords, line_coords_len);
        pi++;
    }
    DEBUG_PRINT("前面 line_coords=%p\n", line_coords);
}

/*
 * 打印单条 line 的信息
 *   - pi 本来应该写成 li 的，保存线的点数以及各点坐标构成的数组在 line_coords 中的偏移量
 *   - line_coords 第二区（[1]线坐标信息），包含各多边形的线号数组，各线的坐标数组
 *   - line_coords_len 第二区的大小，用于检测越界（合法文件当然不会越界）
 */
static void
print_line_info(struct line_info *pi, void *line_coords, size_t line_coords_len) {
    double *pos;

    DEBUG_PRINT("line 信息\n");
    DEBUG_PRINT("============\n");
    DEBUG_PRINT("int1=%d, int2=%d, 点数=%d, 点坐标存储位置=%d, int3=%d\n", pi->int1, pi->int2, pi->num_points, pi->off_points_coords, pi->int3);
    DEBUG_PRINT("线型号=%d, 辅助线型号=%d, 覆盖方式=%d, 线颜色号=%d\n", pi->line_pattern, pi->aux_line_pattern, pi->cover_type, pi->color_index);
    DEBUG_PRINT("线宽=%f, 线各类=%d, X系数=%f, Y系数=%f, 辅助色=%d\n", pi->line_width, pi->line_type, pi->x_factor, pi->y_factor, pi->aux_color);
    DEBUG_PRINT("图层=%d, int4=%d, int5=%d\n", pi->layer, pi->int4, pi->int5);
    if (pi->off_points_coords >= line_coords_len) {
        err(1, "坐标信息地址超出范围");
    }
    pos = (double *)(line_coords + pi->off_points_coords);
    DEBUG_PRINT("线的各点坐标: ");
    for (int i = 0; i < pi->num_points; i++) {
        DEBUG_PRINT("[%f, ", *pos);
        pos++;
        DEBUG_PRINT("%f], ", *pos);
        pos++;
    }
    DEBUG_PRINT("\n\n");
};

/*
 * 打印每条 line 的信息
 */
static void
print_line_infos(int n, struct line_info *lis, void *line_coords, size_t line_coords_len) {
    struct line_info *li = lis;

    for (int i = 0; i < n; i++) {  // 
        DEBUG_PRINT("line %d:\n", i + 1);  // 它的 line 是从 1 开始编号的
        print_line_info(li, line_coords, line_coords_len);
        li++;
    }
}

/*
 * 从原始属性定义生成UTF-8属性名的版本
 *   - def    原定义
 *   - defu   新结构，内存由调用者负责
 *   - n      属性个数
 *   - icv    iconv 上下文
 * XXX 用了 iconv 上下文，也没有线程安全，没有安全性检查
 */
static void
iconv_attr_def(struct obj_attr_define *def, struct obj_attr_define_utf8 *defu, int n, iconv_t icv) {
    size_t inbufl, outbufl;
    char *inbufp, *outbufp;

    for (int i = 0; i < n; i++) {
        inbufp = def->attr_name;
        inbufl = strlen(def->attr_name);
        outbufp = defu->name_utf8;
        outbufl = sizeof(defu->name_utf8);
        bzero(defu->name_utf8, sizeof(defu->name_utf8));
        iconv(icv, NULL, NULL, NULL, NULL);
        iconv(icv, &inbufp, &inbufl, &outbufp, &outbufl);
        defu->o = *def;
        def++;
        defu++;
    }
}

/*
 * 为一个对象添加其属性值
 *   - ps    GeoJSON 中对象的 properties
 *   - def   各属性定义，UTF-8版
 *   - ndef  属性个数
 *   - attrv 该对象属性值起始点
 *   - icv   iconv 上下文
 * XXX 用了 iconv 上下文，也没有线程安全，没有安全性检查
 */
static void
geojson_add_attrs(cJSON *ps, struct obj_attr_define_utf8 *def, int ndef, void *attrv, iconv_t icv) {
    char utf8_str[512]; // 用于转换后的 UTF8 字符串
    size_t inbufl, outbufl;  // iconv 用
    char *inbufp, *outbufp;  // iconv 用
    char *p = attrv + def->o.attr_off;
    int *val_int;
    double *val_double;
    float *val_float;

    for (int i = 0; i < ndef; i++) {  // 遍历所有属性
        switch (def->o.type) {
        case ATTR_STR:
            iconv(icv, NULL, NULL, NULL, NULL);
            bzero(utf8_str, sizeof(utf8_str));
            inbufp = (char *)p;  // 其实多此一举
            inbufl = strlen(inbufp);
            outbufp = utf8_str;
            outbufl = sizeof(utf8_str);
            iconv(icv, &inbufp, &inbufl, &outbufp, &outbufl);
            cJSON_AddStringToObject(ps, def->name_utf8, utf8_str);
            break;
        case ATTR_INT:
            val_int = (int *)p;
            cJSON_AddNumberToObject(ps, def->name_utf8, *val_int);
            break;
        case ATTR_FLOAT:
            val_float = (float *)p;
            cJSON_AddNumberToObject(ps, def->name_utf8, *val_float);
            break;
        case ATTR_DOUBLE:
            val_double = (double *)p;
            cJSON_AddNumberToObject(ps, def->name_utf8, *val_double);
            break;
        default:
            DEBUG_PRINT("未知的属性类型 %d\n", def->o.type);
        }

        //p += def->o.size;
        def++;
        p = attrv + def->o.attr_off;
    }
}

/*
 * 打印属性区头部信息和属性定义信息, 我们假设头部后面紧跟的属性定义是存在的
 */
static void
print_attr_header(struct obj_attr_header *h) {
    DEBUG_PRINT("属性值区偏移量: %d(0x%x)，属性数: %d, 属性值区头部（估计存放属性缺省值）大小：%d(0x%x)\n", h->off_attr_value, h->off_attr_value, h->num_attrs,
            h->attrs_size, h->attrs_size);

    struct obj_attr_define *def = (struct obj_attr_define *)((char *)h + sizeof(*h));
    char utf8_str[32]; // 用于转换后的 UTF8 字符串
    size_t inbufl, outbufl;
    char *inbufp, *outbufp;
    iconv_t icv = iconv_open("UTF-8", "GB18030");
    if (icv == (iconv_t)-1) {
        DEBUG_PRINT("iconv 初始化失败");
    }

    for (int i = 0; i < h->num_attrs; i++) {
        if (icv != (iconv_t)-1) {
            iconv(icv, NULL, NULL, NULL, NULL);
            bzero(utf8_str, sizeof(utf8_str));
            inbufp = def->attr_name;
            outbufp = utf8_str;
            inbufl = strlen(def->attr_name);
            outbufl = sizeof(utf8_str);
            iconv(icv, &inbufp, &inbufl, &outbufp, &outbufl); 
        } else {
            strncpy(utf8_str, def->attr_name, 19);
        }
        DEBUG_PRINT("属性名=%s, 属性类型=%d，占用空间=%d, 序号=%d\n", utf8_str, def->type, def->size, def->index);
        def++;
    }
}

/*
 * 把一条线上的各点坐标加入多边形的坐标数组中
 *   - cs 环
 *   - li 线信息
 *   - reverse 是否要从尾部逆着加入各点坐标
 *   - line_coords 第二区（[1]线坐标信息），包含各多边形的线号数组，各线的坐标数组
 */
static void
poly_add_line(cJSON *cs, struct line_info *li, int reverse, void *line_coords) {
    double *pos;  // 指向单个坐标分量的 double
    int num_p = li->num_points;  // XXX 点数，没判断合法性
    cJSON *p, *x, *y;
    int step;  // 正序时步长为 2，逆序时为 -2

    if (!reverse) {  // 正序
        pos = (double *)(line_coords + li->off_points_coords);
        step = 2;
    } else {
        pos = (double *)(line_coords + li->off_points_coords) + 2 * (num_p - 1);
        step = -2;
    }

    int points = cJSON_GetArraySize(cs);  // 当前环总点数, 新开的环为0，否则正常至少是 2
    if (points > 0) {  // 非空环
        // 比较一下当前最后一点与我们要新加的第一点是否重合
        cJSON *last = cJSON_GetArrayItem(cs, points - 1);
        double x_last = (cJSON_GetArrayItem(last, 0))->valuedouble;
        double y_last = (cJSON_GetArrayItem(last, 1))->valuedouble;

        if (*pos == x_last && *(pos + 1) == y_last) { // 重合了，跳过第一点
            num_p--;
            pos += step;
            DEBUG_PRINT("跳过弧段终点的重合\n");
        }
    }
    for (int i = 0; i < num_p; i++) {
        p = cJSON_CreateArray();

        x = cJSON_CreateNumber(*pos);
        cJSON_AddItemToArray(p, x);
        y = cJSON_CreateNumber(*(pos + 1));
        cJSON_AddItemToArray(p, y);
        pos += step;

        cJSON_AddItemToArray(cs, p);
    }
}

static double
distance(double x1, double y1, double x2, double y2) {
    return sqrt((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2));
}

double small_double = 0.000001;
/*
 * Make Coordinates Ring
 * 使用坐标构成一个环，也就是保证最后一点与第一点相同
 */
static void
make_cs_ring(cJSON *r) {
    int points = cJSON_GetArraySize(r);  // 总点数, 正常至少是 2

    // 检查是否成环，如没成环则要追加第一个点的坐标到最后以让它成环
    cJSON *first = cJSON_GetArrayItem(r, 0);
    cJSON *last = cJSON_GetArrayItem(r, points - 1);
    double x_first = (cJSON_GetArrayItem(first, 0))->valuedouble;
    double y_first = (cJSON_GetArrayItem(first, 1))->valuedouble;
    double x_last = (cJSON_GetArrayItem(last, 0))->valuedouble;
    double y_last = (cJSON_GetArrayItem(last, 1))->valuedouble;

    if (x_first != x_last || y_first != y_last) {
        if (distance(x_first, y_first, x_last, y_last) < small_double) {
            DEBUG_PRINT("首尾点非常接近: %.6f, %.6f : %.6f, %.6f\n", x_first, y_first, x_last, y_last);
        }
        cJSON *p = cJSON_CreateArray();
        cJSON_AddItemToArray(p, cJSON_CreateNumber(x_first));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(y_first));
        cJSON_AddItemToArray(r, p);
    }

    // 检查一下最后两点之间距离
    //cJSON *last2 = cJSON_GetArrayItem(r, points - 2);  // points 没改过，所以是倒数第2点
    //double x_last2 = (cJSON_GetArrayItem(last2, 0))->valuedouble;
    //double y_last2 = (cJSON_GetArrayItem(last2, 1))->valuedouble;
    //if (distance(x_last2, y_last2, x_last, y_last) < small_double) {
        //DEBUG_PRINT("最后两点非常接近\n");
    //}
}

/*
 * 从 CMY 的某分量 x 中去除 k 分量
 */
static unsigned char
remove_k(unsigned char k, unsigned char x) {
    // 肯定不会出现溢出现象
    return (unsigned char)round(255.0 - (255.0 - k) * (255.0 - x) / 255.0);
}

/*
 * 把一个 KCMY 转成 K 为 0 的等价 CMY 表示
 */
static struct color_def kcmy_nok(struct color_def *k) {
    struct color_def cmy;

    if (k->k == 0) {  // 本来就没有 K 分量
        return *k;
    }
    cmy.k = 0;
    cmy.c = remove_k(k->k, k->c);
    cmy.m = remove_k(k->k, k->m);
    cmy.y = remove_k(k->k, k->y);

    return cmy;
}

/*
 * 把一个专色分量转成无黑选等价 CMY 表示
 *   zs    专色定义
 *   z     专色分量，0 - 255
 */
static struct color_def zs_nok(struct color_def *zs, unsigned char z) {
    struct color_def cmy;

    cmy.k = (unsigned char)round(zs->k * z / 255);
    cmy.c = (unsigned char)round(zs->c * z / 255);
    cmy.m = (unsigned char)round(zs->m * z / 255);
    cmy.y = (unsigned char)round(zs->y * z / 255);

    return kcmy_nok(&cmy);
}

/*
 * 无黑 CMY 相加
 */
static struct color_def cmy_add(struct color_def *a, struct color_def *b) {
    struct color_def r;
    int i;

    r.k = 0;  // 就不检查 a b 的 K 分量是否为 0 了
    i = a->c + b->c;
    r.c = i > 255 ? 255 : i;
    i = a->m + b->m;
    r.m = i > 255 ? 255 : i;
    i = a->y + b->y;
    r.y = i > 255 ? 255 : i;

    return r;
}

/*
 * 无黑 CMY 转成 RGB
 */
static struct color_rgb cmy_to_rgb(struct color_def *k) {
    struct color_rgb r;

    r.r = 255 - k->c;
    r.g = 255 - k->m;
    r.b = 255 - k->y;

    return r;
}

/*
 * 把一个 MapGIS 的 KCMY 定义转成 RGB 表示
 *   k     此色的 KCMY值 和 专色分量值
 *   ch    Pcolor.lib 头部，有专色数量及定义
 *   rgb   结果 RGB 值
 */
static void
kcmy_to_rgb(struct pcolor_def *k, struct pcolor_header *ch, struct color_rgb *rgb) {
    //unsigned char k0, c0, m0, y0;
    struct color_def k0, zs;

    // KCMY 去黑
    k0 = kcmy_nok(&k->kcmy);

    // 专色分量去黑并累加
    for (int i = 0; i < ch->colors_zs; i++) {  // XXX 没检查合法性
        zs = zs_nok(&ch->zs[i], k->zs[i]);  // 专色分量去黑
        k0 = cmy_add(&k0, &zs);
    }
    *rgb = cmy_to_rgb(&k0);
}

/*
 * 生成 GeoJSON 文件
 *   - fh 文件头部信息，从中得到总的线数，多边形数
 *   - lis 第一个区（[0]线信息），包含每条线的索引信息，它由几个点构成，点坐标数组在第二区的偏移量
 *   - pis 第九个区（[8]多边形信息），包含多边形信息：它由几条线构成，线号数组在第二区的偏移量
 *   - line_coords 第二区（[1]线坐标信息），包含各多边形的线号数组，各线的坐标数组
 *   - attr 属性区
 *   - pcolor_table 从 Pcolor.lib 文件中读出来的颜色表
 *   - pcolor_max 最大颜色号 + 1，目前没用它进行判断
 */
static void
gen_geojson(const char *name, struct file_header *fh, struct line_info *lis, struct polygon_info *pis, void *line_coords, void *attr,
        struct pcolor_header *pcolh, struct pcolor_def *pcolor_table, int pcolor_max) {
    int num_total_lines = fh->num_lines;
    int num_total_polys = fh->num_polygons;
    struct polygon_info *pi = pis + 1;  // 真正的数据是从第二块开始的
    iconv_t icv = iconv_open("UTF-8", "GB18030");  // 用于属性名和属性值的编码，从GB2312到UTF-8，XXX 没 close
    cJSON *gj = cJSON_CreateObject();  // GeoJSON
    cJSON *fs = cJSON_CreateArray();  // features

    cJSON_AddStringToObject(gj, "type", "FeatureCollection");
    cJSON_AddStringToObject(gj, "name", name);

    // 老的一般采用 北京1954 坐标系，所以我们就缺省生成老版本的 GeoJSON 文件，带坐标系的
    cJSON *crs = cJSON_CreateObject();
    cJSON_AddStringToObject(crs, "type", "name");
    cJSON *crs_prop = cJSON_CreateObject();  // crs 下的 properties 对象
    cJSON_AddStringToObject(crs_prop, "name", "urn:ogc:def:crs:EPSG::4214");
    cJSON_AddItemToObject(crs, "properties", crs_prop);
    cJSON_AddItemToObject(gj, "crs", crs);

    // 属性
    cJSON_AddItemToObject(gj, "features", fs);
    // 先把属性名转成 UTF-8
    struct obj_attr_header *ah = (struct obj_attr_header *)attr;
    struct obj_attr_define *def = (struct obj_attr_define *)(attr + sizeof(*ah));
    struct obj_attr_define_utf8 *defu = (struct obj_attr_define_utf8 *)malloc(sizeof(*defu) * ah->num_attrs);
    iconv_attr_def(def, defu, ah->num_attrs, icv);  // 做 UTF-8 转换
    char *attr_values = (char *)(attr + ah->off_attr_value + ah->attrs_size);

    for (int i = 0; i < num_total_polys; i++) {  // 遍历所有的多边形
        cJSON *f = cJSON_CreateObject();  // Feature
        cJSON *ps = cJSON_CreateObject();  // properties
        cJSON *gm = cJSON_CreateObject();  // geometry
        cJSON *cs = cJSON_CreateArray();  // coordinates
        cJSON *ring = cJSON_CreateArray();  // 多边形环，这里先分配外环，后面遇到一个0再分配一个新的环，后面的环都是要从外环抠除的

        geojson_add_attrs(ps, defu, ah->num_attrs, attr_values, icv);  // 该多边形的属性

        //cJSON_AddNumberToObject(ps, "FillIndex", pi->color);  // 多边形填充色号
        char fillstr[32];  // 形如 [120, 220, 22, 255] 的字符串，表示填充色的 RGBA 值
        struct color_rgb rgb2;  // 转换后的 RGB 值
        struct pcolor_def *pdef = pcolor_table + pi->color - 1;

        kcmy_to_rgb(pdef, pcolh, &rgb2);  // 将 MapGIS 的 4 字节 KCMY 和后续 专色分量 转成 RGB 值
        snprintf(fillstr, sizeof(fillstr) - 1, "%d, %d, %d, 255", rgb2.r, rgb2.g, rgb2.b);
        DEBUG_PRINT("多边形 %d, 色号 %d, fileoff=0x%lx, KCMY=%d,%d,%d,%d,%d,%d RGBA=%s\n", i + 1, pi->color,
                sizeof(struct pcolor_header) + (pi->color - 1) * sizeof(struct pcolor_def), pdef->kcmy.k, pdef->kcmy.c,
                pdef->kcmy.m, pdef->kcmy.y, pdef->zs[0], pdef->zs[1], fillstr);
        cJSON_AddStringToObject(ps, "FillRGB", fillstr);  // 适合于 QGIS 用来填充颜色

        // 坐标
        // MapGIS 6 可能只有多边形，没有多多边形。多边形由一个闭合区（外环）及其中任意个洞（当然也是闭合区）构成
        // 线号 0 用于分隔闭合区，每个闭合区可由1条或多条线构成，第一个闭合区是所谓外环，后续的闭合区是从外环中抠除的洞
        cJSON_AddItemToArray(cs, ring); // 先把外环放进去

        int num_lines = pi->num_lines;  // 本多边形的线数（应该包含特殊的：第一个所谓线号其实是总点数，以及线号为0的线）
        int *line_num;  // 指向线号的指针

        line_num = (int *)(line_coords + pi->off_line_info) + 1;  // 第一个数应该是构成多边形的各线的总点数，+ 1 跳过
        //DEBUG_PRINT("线号存储偏移量：%d\n", pi->off_line_info);
        //DEBUG_PRINT("构造多边形 %d\n", i + 1);
        for (int j = 0; j < (num_lines - 1); j++) {  // 遍历该多边形所有的线（弧段）
            int reverse;  // 负的线号表示要逆过来
            int ln; // 非负线号
            struct line_info *li;

            if (*line_num == 0) {  // 此环结束，检查如不是闭环则添加一点使其闭合，然后再开一个新环
                make_cs_ring(ring);  // 使之闭合
                ring = cJSON_CreateArray();  // 新开一个环
                cJSON_AddItemToArray(cs, ring);  // 把这个闭合区放进 coordinates 数组中
                line_num++;
                continue;
            }
            // 看线号的正负
            if (*line_num < 0) {
                ln = - *line_num;
                reverse = 1;  // 将来要逆过来取坐标
            } else {
                ln = *line_num;
                reverse = 0;
            }
            li = lis + (ln - 1);  // 取线信息，线号是从 1 开始编号的
            //cJSON *hole_j = cJSON_CreateArray();  // XXX 应该是垃圾代码
            poly_add_line(ring, li, reverse, line_coords);
            line_num++;
        }

        cJSON_AddStringToObject(gm, "type", "Polygon");
        cJSON_AddItemToObject(gm, "coordinates", cs);

        cJSON_AddStringToObject(f, "type", "Feature");
        cJSON_AddItemToObject(f, "properties", ps);
        cJSON_AddItemToObject(f, "geometry", gm);

        cJSON_AddItemToArray(fs, f);
        attr_values += ah->attrs_size;
        pi++;
    }

    char *str = cJSON_Print(gj);
    printf("%s", str);
    free(str);
    cJSON_Delete(gj);
    free(defu);
}

int
main(int argc, char **argv) {
    ssize_t r;
    size_t len;
    off_t off;
    char *file_name;
    struct file_header fh;
    struct data_headers dhs;
    struct polygon_info *pis;
    void *line_coords;
    struct line_info *lis;
    size_t line_coords_len;
    void *attr;
    struct obj_attr_header *attr_header;

    if (argc < 2) {
        DEBUG_PRINT("Usage: %s <file>\n", argv[0]);
        return 1;
    }
    file_name = argv[1];
    int fd = open(file_name, O_RDONLY);
    if (fd == -1) {
        err(1, "Open file %s failed", argv[1]);
    }
    r = read(fd, &fh, sizeof(fh));
    if (r != sizeof(fh)) {
        err(1, "Read file header failed");
    }
    g_num_line = fh.num_lines;  // 设置总线数

    print_fh(&fh);
    off = lseek(fd, fh.off_data_headers, SEEK_SET);
    r = read(fd, &dhs, sizeof(dhs));
    if (r != sizeof(dhs)) {
        err(1, "Read data headers failed");
    }
    print_dhs(fh.ftype_id, &dhs);

    // 这里包含有区信息：每个区所属的线的编号连续存放
    // 这里包含线信息：每条线所属点坐标连续存放
    line_coords_len = dhs.line_coords_or_point_string.data_len;
    line_coords = malloc(line_coords_len);
    off = lseek(fd, dhs.line_coords_or_point_string.data_offset, SEEK_SET);
    r = read(fd, line_coords, line_coords_len);
    if (r != line_coords_len) {
        err(1, "读线坐标数据出错");
    }

    len = sizeof(*pis) * (fh.num_polygons + 1);
    pis = (struct polygon_info *)malloc(len);
    off = lseek(fd, dhs.polygon_info.data_offset, SEEK_SET);
    r = read(fd, pis, len);
    if (r != len) {
        err(1, "读区信息出错");
    }
    print_polygon_infos(fh.num_polygons, pis, line_coords, line_coords_len);

    // 读第一个数据区，line info 区（对多边形文件而言）
    // 这个区里包含有由大小为57字节的线信息结构构成的结构数组，该线信息结构中包含：
    //   - 此线包含几个点
    //   - 此线的点坐标在第二区（包含各点的坐标值）的偏移量
    len = sizeof(*lis) * (fh.num_lines + 1);
    lis = (struct line_info *)malloc(len);
    off = lseek(fd, dhs.line_or_point_info.data_offset, SEEK_SET);
    r = read(fd, lis, len);
    if (r != len) {
        err(1, "读线信息区出错");
    }
    lis = (struct line_info *)( (char *)lis + 59);  // 奇怪的偏移量，造成了2字节的越界
    print_line_infos(fh.num_lines, lis, line_coords, line_coords_len);

    len = dhs.polygon_attr.data_len;
    attr = malloc(len);
    off = lseek(fd, dhs.polygon_attr.data_offset, SEEK_SET);
    r = read(fd, attr, len);
    if (r != len) {
        err(1, "读多边形属性区出错");
    }
    attr_header = (struct obj_attr_header *)attr;
    print_attr_header(attr_header);

    struct pcolor_header pcolorh;
    struct pcolor_def *pcolor_table;
    unsigned short pcolor_max;  // 最大许可色号加 1
    int fdc; // Pcolor.lib 文件句柄

    fdc = open("Pcolor.lib", O_RDONLY);
    if (fdc == -1) {
        err(1, "打开色号定义文件 Pcolor.lib 失败");
    }
    r = read(fdc, &pcolorh, sizeof(pcolorh));
    DEBUG_PRINT("读 Pcolor.lib 文件头 %ld 字节\n", r);
    size_t pcolor_table_size = pcolorh.colors * sizeof(*pcolor_table);
    pcolor_table = (struct pcolor_def *)malloc(pcolor_table_size);
    r = read(fdc, pcolor_table, pcolor_table_size);
    if (r != pcolor_table_size) {
        err(1, "读色号定义文件 Pcolor.lib 失败");
    }
    DEBUG_PRINT("读 Pcolor.lib 文件中的 %d 个色标定义共 %ld 字节\n", pcolorh.colors, pcolor_table_size);

    gen_geojson(file_name, &fh, lis, pis, line_coords, attr, &pcolorh, pcolor_table, pcolorh.colors);
    //free(lis);
    free(pis);

    //DEBUG_PRINT("float 的大小为：%ld\n", sizeof(float));
    //DEBUG_PRINT("polygon_info 大小：%ld\n", sizeof(struct polygon_info));
    //DEBUG_PRINT("line_info 大小：%ld\n", sizeof(struct line_info));
    //DEBUG_PRINT("属性区头部大小：%ld(0x%lx)\n", sizeof(struct obj_attr_header), sizeof(struct obj_attr_header));
    //DEBUG_PRINT("属性定义大小：%ld(0x%lx)\n", sizeof(struct obj_attr_define), sizeof(struct obj_attr_define));
    return 0;
}

