/*
 * MapGIS 6.7 file utility
 *
 */

#include <stdio.h>
#include <sys/types.h>  // open()
#include <sys/stat.h>  // open()
#include <fcntl.h>  // open()
#include <err.h>  // err()
#include <unistd.h>  // read()
#include <stdlib.h>  // malloc() and free()
#include <iconv.h>  // iconv_open(), iconv()
#include <string.h>  // strncpy()
#include <strings.h>  // bzero()

#include "cJSON.h"
#include "mapgisf.h"

int g_num_line = 0; // 总线数，主e要用于判断线号越界

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
    printf("文件头信息\n");
    printf("==========\n");
    printf("Type: %s\n", file_type_names[type_id]);
    printf("data headers 偏移量: %d, guess_num_data_headers: %d\n", fh->off_data_headers, fh->guess_num_data_headers);
    printf("线数: %d(%d), 点数: %d(%d), 多边形数: %d(%d)\n", fh->num_lines, fh->num_lines_pad, fh->num_points, fh->num_points_pad,
            fh->num_polygons, fh->num_polygons_pad);
    printf("xmin: %f, ymin: %f, xmax: %f, ymax: %f\n", fh->xmin, fh->ymin, fh->xmax, fh->ymax);
    printf("\n");
}

static void
print_dh(struct data_header *dh) {
    printf("起始:%d:(0x%x):结束:%d:(0x%x):大小:%d(0x%x)\n", dh->data_offset, dh->data_offset,
            dh->data_offset + dh->data_len - 1, dh->data_offset + dh->data_len - 1, dh->data_len, dh->data_len);
}

static void
print_dhs(int ft /* file type */, struct data_headers *dhs) {
    printf("数据区头信息，相当于一个目录\n");
    printf("============================\n");
    if (ft == MAPGIS_F_TYPE_POINT) {
        printf("点信息(点文件的): ");
    } else {
        printf("线信息(面、线文件的): ");
    }
    print_dh(&dhs->line_or_point_info);

    if (ft == MAPGIS_F_TYPE_POINT) {
        printf("点字符串(点文件的): ");
    } else {
        printf("线坐标点(面、线文件的): ");
    }
    print_dh(&dhs->line_coords_or_point_string);

    if (ft == MAPGIS_F_TYPE_POINT) {
        printf("点属性(点文件的): ");
    } else {
        printf("线属性(面、线文件的): ");
    }
    print_dh(&dhs->line_or_point_attr);

    printf("线拓扑关系: ");
    print_dh(&dhs->line_topo_relation);

    printf("结点信息: ");
    print_dh(&dhs->node_info);

    printf("结点属性: ");
    print_dh(&dhs->node_attr);

    printf("unknown信息: ");
    print_dh(&dhs->unknown_info);

    printf("unknown属性: ");
    print_dh(&dhs->unknown_attr);

    printf("多边形信息: ");
    print_dh(&dhs->polygon_info);

    printf("多边形属性: ");
    print_dh(&dhs->polygon_attr);

    printf("pad属性: ");
    print_dh(&dhs->pad);

    printf("pad1属性: ");
    print_dh(&dhs->pad1);

    printf("pad12属性: ");
    print_dh(&dhs->pad12);

    printf("pad13属性: ");
    print_dh(&dhs->pad13);

    printf("pad14属性: ");
    print_dh(&dhs->pad14);

    printf("pad15属性: ");
    print_dh(&dhs->pad15);
    printf("\n");
}

/*
 * 打印单个 polygon 的信息
 */
static void
print_polygon_info(struct polygon_info *pi, void *line_coords, size_t line_coords_len) {
    int *line_num;  // 线号

    printf("polygon 信息\n");
    printf("============\n");
    printf("flag=%d, 线总数=%d, 线号存储位置=%d, 颜色=%f, 填充图案号=%d, 图案高=%f, 图案宽=%f\n", pi->flag, pi->num_lines, pi->off_line_info, pi->color,
            pi->fill_pattern_index, pi->pattern_height, pi->pattern_width);
    printf("笔宽=%d, 图案颜色=%d, 透明输出=%d, 图层=%d, 线号1=%d, 线号2=%d\n", pi->pen_width, pi->pattern_color, pi->transparent_output, pi->layer,
            pi->line_index1, pi->line_index2);
    if (pi->off_line_info >= line_coords_len) {
        err(1, "线号信息超出范围");
    }
    line_num = (int *)(line_coords + pi->off_line_info);
    for (int i = 0; i < pi->num_lines; i++) {
        printf("线号 %d: %d(0x%x)", i, *line_num, *line_num);
        if (*line_num > g_num_line) {
            printf(" 越界，最大 %d\n", g_num_line);
        } else {
            printf("\n");
        }
        line_num++;
    }
    printf("\n");
};

/*
 * 打印每个 polygon 的信息
 */
static void
print_polygon_infos(int n, struct polygon_info *pis, void *line_coords, size_t line_coords_len) {
    struct polygon_info *pi = pis;

    for (int i = 0; i <= n; i++) {  // 故意这么写的，多了一次循环
        printf("Polygon %d:\n", i);
        print_polygon_info(pi, line_coords, line_coords_len);
        pi++;
    }
    printf("前面 line_coords=%p\n", line_coords);
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

    printf("line 信息\n");
    printf("============\n");
    printf("int1=%d, int2=%d, 点数=%d, 点坐标存储位置=%d, int3=%d\n", pi->int1, pi->int2, pi->num_points, pi->off_points_coords, pi->int3);
    printf("线型号=%d, 辅助线型号=%d, 覆盖方式=%d, 线颜色号=%d\n", pi->line_pattern, pi->aux_line_pattern, pi->cover_type, pi->color_index);
    printf("线宽=%f, 线各类=%d, X系数=%f, Y系数=%f, 辅助色=%d\n", pi->line_width, pi->line_type, pi->x_factor, pi->y_factor, pi->aux_color);
    printf("图层=%d, int4=%d, int5=%d\n", pi->layer, pi->int4, pi->int5);
    if (pi->off_points_coords >= line_coords_len) {
        err(1, "坐标信息地址超出范围");
    }
    pos = (double *)(line_coords + pi->off_points_coords);
    printf("线的各点坐标: ");
    for (int i = 0; i < pi->num_points; i++) {
        printf("[%f, ", *pos);
        pos++;
        printf("%f], ", *pos);
        pos++;
    }
    printf("\n\n");
};

/*
 * 打印每条 line 的信息
 */
static void
print_line_infos(int n, struct line_info *lis, void *line_coords, size_t line_coords_len) {
    struct line_info *li = lis;

    for (int i = 0; i < n; i++) {  // 
        printf("line %d:\n", i + 1);  // 它的 line 是从 1 开始编号的
        print_line_info(li, line_coords, line_coords_len);
        li++;
    }
}

/*
 * 打印属性区头部信息, 包括属性定义信息, 我们假设头部后面紧跟的属性定义是存在的
 */
static void
print_attr_header(struct obj_attr_header *h) {
    printf("属性值区偏移量: %d(0x%x)，属性数: %d, 属性值区头部大小：%d(0x%x)\n", h->off_attr_value, h->off_attr_value, h->num_attrs,
            h->header_size_attr_value, h->header_size_attr_value);

    struct obj_attr_define *def = (struct obj_attr_define *)((char *)h + sizeof(*h));
    char utf8_str[32]; // 用于转换后的 UTF8 字符串
    size_t inbufl, outbufl;
    char *inbufp, *outbufp;
    iconv_t icv = iconv_open("UTF-8", "GB18030");
    if (icv == (iconv_t)-1) {
        printf("iconv 初始化失败");
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
        printf("属性名=%s, 属性类型=%d，占用空间=%d, 序号=%d\n", utf8_str, def->type, def->size, def->index);
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

/*
 * Make Coordinates Ring
 * 使用坐标构成一个环，也就是保证最后一点与第一点相同
 */
static void
make_cs_ring(cJSON *r) {
    // 检查是否成环，如没成环则要追加第一个点的坐标到最后以让它成环
    cJSON *first = cJSON_GetArrayItem(r, 0);
    cJSON *last = cJSON_GetArrayItem(r, cJSON_GetArraySize(r) - 1);
    double x_first = (cJSON_GetArrayItem(first, 0))->valuedouble;
    double y_first = (cJSON_GetArrayItem(first, 1))->valuedouble;
    double x_last = (cJSON_GetArrayItem(last, 0))->valuedouble;
    double y_last = (cJSON_GetArrayItem(last, 1))->valuedouble;

    if (x_first != x_last || y_first != y_last) {
        cJSON *p = cJSON_CreateArray();
        cJSON_AddItemToArray(p, cJSON_CreateNumber(x_first));
        cJSON_AddItemToArray(p, cJSON_CreateNumber(y_first));
        cJSON_AddItemToArray(r, p);
    }
}

/*
 * 生成 GeoJSON 文件
 *   - fh 文件头部信息，从中得到总的线数，多边形数
 *   - lis 第一个区（[0]线信息），包含每条线的索引信息，它由几个点构成，点坐标数组在第二区的偏移量
 *   - pis 第九个区（[8]多边形信息），包含多边形信息：它由几条线构成，线号数组在第二区的偏移量
 *   - line_coords 第二区（[1]线坐标信息），包含各多边形的线号数组，各线的坐标数组
 *   - attr 属性区
 */
static void
gen_geojson(struct file_header *fh, struct line_info *lis, struct polygon_info *pis, void *line_coords, void *attr) {
    int num_total_lines = fh->num_lines;
    int num_total_polys = fh->num_polygons;
    struct polygon_info *pi = pis + 1;  // 真正的数据是从第二块开始的
    char *str = NULL;
    cJSON *gj = cJSON_CreateObject();  // GeoJSON
    cJSON *fs = cJSON_CreateArray();  // features

    cJSON_AddStringToObject(gj, "type", "FeatureCollection");
    cJSON_AddStringToObject(gj, "name", "GeoJSON_test");
    cJSON_AddItemToObject(gj, "features", fs);

    for (int i = 0; i < num_total_polys; i++) {  // 遍历所有的多边形
        cJSON *f = cJSON_CreateObject();  // Feature
        cJSON *ps = cJSON_CreateObject();  // properties
        cJSON *gm = cJSON_CreateObject();  // geometry
        cJSON *cs = cJSON_CreateArray();  // coordinates
        cJSON *ring = cJSON_CreateArray();  // 多边形环，这里先分配外环，后面遇到一个0再分配一个新的环，后面的环都是要从外环抠除的

        cJSON_AddNumberToObject(ps, "ID", i + 1);  // 模拟一个假的属性，它的序号从1开始编号

        // 坐标
        // MapGIS 6 可能只有多边形，没有多多边形。多边形由一个闭合区（外环）及其中任意个洞（当然也是闭合区）构成
        // 线号 0 用于分隔闭合区，每个闭合区可由1条或多条线构成，第一个闭合区是所谓外环，后续的闭合区是从外环中抠除的洞
        cJSON_AddItemToArray(cs, ring); // 先把外环放进去

        int num_lines = pi->num_lines;  // 本多边形的线数（应该包含特殊的：第一个所谓线号其实是总点数，以及线号为0的线）
        int *line_num;  // 指向线号的指针

        line_num = (int *)(line_coords + pi->off_line_info) + 1;  // 第一个数应该是构成多边形的各线的总点数，+ 1 跳过
        //printf("线号存储偏移量：%d\n", pi->off_line_info);
        printf("构造多边形 %d\n", i + 1);
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
            cJSON *hole_j = cJSON_CreateArray();
            poly_add_line(ring, li, reverse, line_coords);
            line_num++;
        }

        cJSON_AddStringToObject(gm, "type", "Polygon");
        cJSON_AddItemToObject(gm, "coordinates", cs);

        cJSON_AddStringToObject(f, "type", "Feature");
        cJSON_AddItemToObject(f, "properties", ps);
        cJSON_AddItemToObject(f, "geometry", gm);

        cJSON_AddItemToArray(fs, f);
        pi++;
    }

    str = cJSON_Print(gj);
    printf("GeoJSON:\n%s\n", str);
    free(str);
    cJSON_Delete(gj);
}

int
main(int argc, char **argv) {
    ssize_t r;
    size_t len;
    off_t off;
    struct file_header fh;
    struct data_headers dhs;
    struct polygon_info *pis;
    void *line_coords;
    struct line_info *lis;
    size_t line_coords_len;
    void *attr;
    struct obj_attr_header *attr_header;

    if (argc < 2) {
        printf("Usage: %s <file>\n", argv[0]);
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
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

    gen_geojson(&fh, lis, pis, line_coords, attr);
    //free(lis);
    free(pis);

    //printf("float 的大小为：%ld\n", sizeof(float));
    //printf("polygon_info 大小：%ld\n", sizeof(struct polygon_info));
    //printf("line_info 大小：%ld\n", sizeof(struct line_info));
    //printf("属性区头部大小：%ld(0x%lx)\n", sizeof(struct obj_attr_header), sizeof(struct obj_attr_header));
    //printf("属性定义大小：%ld(0x%lx)\n", sizeof(struct obj_attr_define), sizeof(struct obj_attr_define));
    return 0;
}

