/*
 * 整个文件的头部
 */
struct __attribute__((packed)) file_header {
    char  ftype[8];  // .WL = WMAP`D21，.WT = WMAP`D22，.WP = WMAP`D23
    int ftype_id;  // .WL = 0, .WT = 1, .WP = 2
    int off_data_headers;  // data_headers 的偏移量
    int guess_num_data_headers;
    char reserved_240[240];
    int num_lines;  // 线数
    int num_lines_pad;
    int num_points;  // 点数
    int num_points_pad;
    int num_polygons;  // 多边形数
    int num_polygons_pad;
    int num_pad3[5];
    double xmin;
    double ymin;
    double xmax;
    double ymax;
};

// 文件类型 ftype_id 到 名字的映射
const char *file_type_names[] = {
    "Line",
    "Point",
    "Polygon"
};

#define MAPGIS_F_TYPE_LINE     0
#define MAPGIS_F_TYPE_POINT    1
#define MAPGIS_F_TYPE_POLYGON  2

/*
 * 由文件头中的 off_data_headers 指定的偏移量开始，是一个下列结构的数组
 * 猜测数组的长度是文件头中的 guess_num_data_headers
 *
 * 此结构是每个数据区的头（集中在一起了），指明每个数据区的偏移量和字节数大小
 */
struct __attribute__ ((packed)) data_header {
    int data_offset;
    int data_len;
    short pad;  // 0xff 0xff
};

/*
 * 由文件头中的 off_data_headers 指定的偏移量，数据区头数组
 */
struct __attribute__ ((packed)) data_headers {
    struct data_header line_or_point_info;
    struct data_header line_coords_or_point_string;
    struct data_header line_or_point_attr;
    struct data_header line_topo_relation;
    struct data_header node_info;
    struct data_header node_attr;
    struct data_header unknown_info;
    struct data_header unknown_attr;
    struct data_header polygon_info;  // 多边形信息区，每个多边形 40 个字节的信息，结构为 struct polygon_info
    struct data_header polygon_attr;
    struct data_header pad;
    struct data_header pad1;
    struct data_header pad12;
    struct data_header pad13;
    struct data_header pad14;
    struct data_header pad15;
};

/*
 * 40 字节的多边形信息，每个多边形由一个线所围合，所以它要引用线的定义
 * 此结构数组的起始位置由 data_headers[8].data_offset 决定
 */
struct __attribute__ ((packed)) polygon_info {
    unsigned char flag;  // 应该都是 0x1 ?
    int num_lines;  // 线总数
    int off_line_info;  /* 线号存储位置，以线坐标点数据区 data_headers[1].data_offset 为起始点的偏移量, 在那里连续存储了 num_lines 个线号
                         * 每个线号是一个 4 字节整数，负线号表示该线的点序列要反过来 */
    int color;  // 区（多边形）颜色，int 类型的大小为 4 字节
    unsigned short fill_pattern_index;  // 填充图案号
    float pattern_height;  // 图案高
    float pattern_width;  // 图案宽
    unsigned short pen_width;  // 笔宽
    unsigned int pattern_color;  // 图案颜色
    unsigned char transparent_output;  // 透明输出
    unsigned short layer;  // 图层
    int line_index1;
    int line_index2;
};

/*
 * 57 字节的线信息，包括点的个数，点坐标存储位置（与多边形信息一样，也是以 data_headers[1].data_offset 为起点的偏移量来表示）
 * 此结构数组的起始位置由 data_headers[0].dat_offset 决定
 */
struct __attribute__ ((packed)) line_info {
    int int1;  // 未知整数
    int int2;  // 未知整数
    int num_points;  // 点数
    int off_points_coords;  /* 各点坐标存储位置，以线坐标点数据区 data_headers[1].data_offset 为起始点的偏移量, 在那里连续存储了 num_points 个点的
                             * 坐标，每个坐标由两个 8 字节 double 数组成 */
    int int3;  // 未知
    short line_pattern;  // 线型号
    char aux_line_pattern;  // 辅助线型号
    char cover_type;  // 覆盖方式
    int color_index;  // 线颜色号
    float line_width;  // 线宽
    char line_type;  // 线种类
    float x_factor;  // X系数
    float y_factor;  // Y系数
    int aux_color;  // 辅助色
    int layer;  // 图层
    int int4;
    int int5;
};

/*
 * 各(至少线和多边形)属性区头部结构，它指明了有多少个属性，属性值区偏移量，属性块大小等信息
 */
struct __attribute__ ((packed)) obj_attr_header {
    char unknown_char[12];  // 似乎是固定的 60 44 e1 07 0c 07 00 00  00 00 00 00
    int off_attr_value;  // 属性值表偏移量，以本结构开始为起点
    char unknown_char2[306];
    short num_attrs;  // 属性个数
    int unknown_int;
    int attrs_size;  // 属性块大小，检查过几个，都比所有属性值占空间的和大1
    char unknown_char3[16];  // 全 0
    // 后面接着属性定义数组
};

/*
 * 属性定义
 */
struct __attribute__ ((packed)) obj_attr_define {
    char attr_name[20];  // 属性名，应该是包含一个结尾的 0 的
    char type; // ATTR_STR 0, ATTR_INT 3, ATTR_FLOAT 4, ATTR_DOUBLE 5
    int attr_off; // 该属性在一行属性组中的字节偏移量，似乎从1开始
    short size;  // 属性值占用空间大小
    short size2; // 对整数：位数，浮点数：小数点前位数，字符串：最大字符串长度（一般比size小1）
    char size3; // 对浮点数：小数点后位数
    char unknown[3]; // 似乎对浮点数：02 00 00，而其它：01 00 00
    int index; // 属性序号，好像从0开始编号
    short unknown_short; // 似乎都是 00 00
};

#define ATTR_STR     0
#define ATTR_INT     3
#define ATTR_FLOAT   4
#define ATTR_DOUBLE  5

/*
 * 属性定义 UTF-8 版本，用于实际使用
 */
struct __attribute__ ((packed)) obj_attr_define_utf8 {
    char name_utf8[64];  // 属性名，UTF-8编码，原来最多9个汉字，也就是27个字节的UTF-8，所以肯定够用了
    struct obj_attr_define o; // 原来的定义
};

/*
 * RGB 颜色定义
 */
struct __attribute__ ((packed)) color_rgb {
    unsigned char r; // 红
    unsigned char g; // 绿
    unsigned char b; // 蓝
};

/*
 * KCMY 颜色定义
 */
struct __attribute__ ((packed)) color_def {
    unsigned char k; // 黑色
    unsigned char c; // 青色
    unsigned char m; // 品红
    unsigned char y; // 黄色
};

/*
 * Pcolor.lib 文件中每个色号的定义，共32个字节，其中前4个字节是 KCMY 值，然后2字节修正，但后面26个字节用途未知
 */
struct __attribute__ ((packed)) pcolor_def {
    unsigned char k; // 黑色
    unsigned char c; // 青色
    unsigned char m; // 品红
    unsigned char y; // 黄色
    unsigned char p;  // 修正
    unsigned char q;  // 修正
    char pad[26];  // 用途未知
};

/*
 * Pcolor.lib 文件格式
 * 头部结构 pcolor_header，然后每个色号32字节，结构为 pcolor_def
 * 文件实际大小可能比从 pcolor_header.colors 计算出来的要大，估计是预先分配了一点空间
 */
struct __attribute__ ((packed)) pcolor_header {
    char headstr[8];  // 特征字符串 "PCOLOR 3"
    short colors;  // 最大色号 + 1，色号从1开始
    short colors_z;  // 专色数量
    struct color_def zs[36];  // 最多 36 个专色，专色是直接用 4 字节来定义，没像其它色标一样用了 32 个字节
};

