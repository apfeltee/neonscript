struct _IO_FILE {
	int                        _flags;               /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	char *                     _IO_read_ptr;         /*     8     8 */
	char *                     _IO_read_end;         /*    16     8 */
	char *                     _IO_read_base;        /*    24     8 */
	char *                     _IO_write_base;       /*    32     8 */
	char *                     _IO_write_ptr;        /*    40     8 */
	char *                     _IO_write_end;        /*    48     8 */
	char *                     _IO_buf_base;         /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	char *                     _IO_buf_end;          /*    64     8 */
	char *                     _IO_save_base;        /*    72     8 */
	char *                     _IO_backup_base;      /*    80     8 */
	char *                     _IO_save_end;         /*    88     8 */
	struct _IO_marker *        _markers;             /*    96     8 */
	struct _IO_FILE *          _chain;               /*   104     8 */
	int                        _fileno;              /*   112     4 */
	int                        _flags2;              /*   116     4 */
	__off_t                    _old_offset;          /*   120     8 */
	/* --- cacheline 2 boundary (128 bytes) --- */
	short unsigned int         _cur_column;          /*   128     2 */
	signed char                _vtable_offset;       /*   130     1 */
	char                       _shortbuf[1];         /*   131     1 */

	/* XXX 4 bytes hole, try to pack */

	_IO_lock_t *               _lock;                /*   136     8 */
	__off64_t                  _offset;              /*   144     8 */
	struct _IO_codecvt *       _codecvt;             /*   152     8 */
	struct _IO_wide_data *     _wide_data;           /*   160     8 */
	struct _IO_FILE *          _freeres_list;        /*   168     8 */
	void *                     _freeres_buf;         /*   176     8 */
	size_t                     __pad5;               /*   184     8 */
	/* --- cacheline 3 boundary (192 bytes) --- */
	int                        _mode;                /*   192     4 */
	char                       _unused2[20];         /*   196    20 */

	/* size: 216, cachelines: 4, members: 29 */
	/* sum members: 208, holes: 2, sum holes: 8 */
	/* last cacheline: 24 bytes */
};
struct winsize {
	short unsigned int         ws_row;               /*     0     2 */
	short unsigned int         ws_col;               /*     2     2 */
	short unsigned int         ws_xpixel;            /*     4     2 */
	short unsigned int         ws_ypixel;            /*     6     2 */

	/* size: 8, cachelines: 1, members: 4 */
	/* last cacheline: 8 bytes */
};
struct termios {
	tcflag_t                   c_iflag;              /*     0     4 */
	tcflag_t                   c_oflag;              /*     4     4 */
	tcflag_t                   c_cflag;              /*     8     4 */
	tcflag_t                   c_lflag;              /*    12     4 */
	cc_t                       c_line;               /*    16     1 */
	cc_t                       c_cc[32];             /*    17    32 */

	/* XXX 3 bytes hole, try to pack */

	speed_t                    c_ispeed;             /*    52     4 */
	speed_t                    c_ospeed;             /*    56     4 */

	/* size: 60, cachelines: 1, members: 8 */
	/* sum members: 57, holes: 1, sum holes: 3 */
	/* last cacheline: 60 bytes */
};
struct LNAppendBuffer {
	char *                     b;                    /*     0     8 */
	int                        len;                  /*     8     4 */

	/* size: 16, cachelines: 1, members: 2 */
	/* padding: 4 */
	/* last cacheline: 16 bytes */
};
struct linenoiseCompletions {
	size_t                     len;                  /*     0     8 */
	char * *                   cvec;                 /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};
struct linenoiseState {
	int                        ifd;                  /*     0     4 */
	int                        ofd;                  /*     4     4 */
	char *                     buf;                  /*     8     8 */
	size_t                     buflen;               /*    16     8 */
	const char  *              prompt;               /*    24     8 */
	size_t                     plen;                 /*    32     8 */
	size_t                     pos;                  /*    40     8 */
	size_t                     oldcolpos;            /*    48     8 */
	size_t                     len;                  /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	size_t                     cols;                 /*    64     8 */
	size_t                     maxrows;              /*    72     8 */
	int                        history_index;        /*    80     4 */

	/* size: 88, cachelines: 2, members: 12 */
	/* padding: 4 */
	/* last cacheline: 24 bytes */
};
struct __va_list_tag {
	unsigned int               gp_offset;            /*     0     4 */
	unsigned int               fp_offset;            /*     4     4 */
	void *                     overflow_arg_area;    /*     8     8 */
	void *                     reg_save_area;        /*    16     8 */

	/* size: 24, cachelines: 1, members: 4 */
	/* last cacheline: 24 bytes */
};
struct timeval {
	__time_t                   tv_sec;               /*     0     8 */
	__suseconds_t              tv_usec;              /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};
struct timespec {
	__time_t                   tv_sec;               /*     0     8 */
	__syscall_slong_t          tv_nsec;              /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};
struct stat {
	__dev_t                    st_dev;               /*     0     8 */
	__ino_t                    st_ino;               /*     8     8 */
	__nlink_t                  st_nlink;             /*    16     8 */
	__mode_t                   st_mode;              /*    24     4 */
	__uid_t                    st_uid;               /*    28     4 */
	__gid_t                    st_gid;               /*    32     4 */
	int                        __pad0;               /*    36     4 */
	__dev_t                    st_rdev;              /*    40     8 */
	__off_t                    st_size;              /*    48     8 */
	__blksize_t                st_blksize;           /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	__blkcnt_t                 st_blocks;            /*    64     8 */
	struct timespec            st_atim;              /*    72    16 */
	struct timespec            st_mtim;              /*    88    16 */
	struct timespec            st_ctim;              /*   104    16 */
	__syscall_slong_t          __glibc_reserved[3];  /*   120    24 */

	/* size: 144, cachelines: 3, members: 15 */
	/* last cacheline: 16 bytes */
};
struct StringBuffer {
	_Bool                      isintern;             /*     0     1 */

	/* XXX 7 bytes hole, try to pack */

	size_t                     length;               /*     8     8 */
	size_t                     capacity;             /*    16     8 */
	char *                     data;                 /*    24     8 */

	/* size: 32, cachelines: 1, members: 4 */
	/* sum members: 25, holes: 1, sum holes: 7 */
	/* last cacheline: 32 bytes */
};
struct optcontext_t {
	char * *                   argv;                 /*     0     8 */
	int                        argc;                 /*     8     4 */
	int                        permute;              /*    12     4 */
	int                        optind;               /*    16     4 */
	int                        optopt;               /*    20     4 */
	char *                     optarg;               /*    24     8 */
	char                       errmsg[64];           /*    32    64 */
	/* --- cacheline 1 boundary (64 bytes) was 32 bytes ago --- */
	int                        subopt;               /*    96     4 */

	/* size: 104, cachelines: 2, members: 8 */
	/* padding: 4 */
	/* last cacheline: 40 bytes */
};
struct optlongflags_t {
	const char  *              longname;             /*     0     8 */
	int                        shortname;            /*     8     4 */
	optargtype_t               argtype;              /*    12     4 */
	const char  *              helptext;             /*    16     8 */

	/* size: 24, cachelines: 1, members: 4 */
	/* last cacheline: 24 bytes */
};
struct FSDirReader {
	void *                     handle;               /*     0     8 */

	/* size: 8, cachelines: 1, members: 1 */
	/* last cacheline: 8 bytes */
};
struct FSDirItem {
	char                       name[1025];           /*     0  1025 */
	/* --- cacheline 16 boundary (1024 bytes) was 1 byte ago --- */
	_Bool                      isdir;                /*  1025     1 */
	_Bool                      isfile;               /*  1026     1 */

	/* size: 1027, cachelines: 17, members: 3 */
	/* last cacheline: 3 bytes */
};
struct NNProcessInfo {
	int                        cliprocessid;         /*     0     4 */

	/* XXX 4 bytes hole, try to pack */

	NNObjArray *               cliargv;              /*     8     8 */
	NNObjString *              clidirectory;         /*    16     8 */
	NNObjFile *                filestdout;           /*    24     8 */
	NNObjFile *                filestderr;           /*    32     8 */
	NNObjFile *                filestdin;            /*    40     8 */

	/* size: 48, cachelines: 1, members: 6 */
	/* sum members: 44, holes: 1, sum holes: 4 */
	/* last cacheline: 48 bytes */
};
struct NNFormatInfo {
	size_t                     fmtlen;               /*     0     8 */
	const char  *              fmtstr;               /*     8     8 */
	NNPrinter *                writer;               /*    16     8 */
	NNState *                  pvm;                  /*    24     8 */

	/* size: 32, cachelines: 1, members: 4 */
	/* last cacheline: 32 bytes */
};
struct NNIOResult {
	_Bool                      success;              /*     0     1 */

	/* XXX 7 bytes hole, try to pack */

	char *                     data;                 /*     8     8 */
	size_t                     length;               /*    16     8 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 17, holes: 1, sum holes: 7 */
	/* last cacheline: 24 bytes */
};
struct NNAstFuncCompiler {
	int                        localcount;           /*     0     4 */
	int                        scopedepth;           /*     4     4 */
	int                        handlercount;         /*     8     4 */
	_Bool                      fromimport;           /*    12     1 */

	/* XXX 3 bytes hole, try to pack */

	NNAstFuncCompiler *        enclosing;            /*    16     8 */
	NNObjFuncScript *          targetfunc;           /*    24     8 */
	NNFuncType                 type;                 /*    32     4 */

	/* XXX 4 bytes hole, try to pack */

	NNAstLocal                 locals[128];          /*    40  4096 */
	/* --- cacheline 64 boundary (4096 bytes) was 40 bytes ago --- */
	NNAstUpvalue               upvalues[128];        /*  4136   512 */

	/* size: 4648, cachelines: 73, members: 9 */
	/* sum members: 4641, holes: 2, sum holes: 7 */
	/* last cacheline: 40 bytes */
};
struct NNObject {
	NNObjType                  type;                 /*     0     4 */
	_Bool                      mark;                 /*     4     1 */

	/* XXX 3 bytes hole, try to pack */

	NNState *                  pvm;                  /*     8     8 */
	_Bool                      stale;                /*    16     1 */

	/* XXX 7 bytes hole, try to pack */

	NNObject *                 next;                 /*    24     8 */

	/* size: 32, cachelines: 1, members: 5 */
	/* sum members: 22, holes: 2, sum holes: 10 */
	/* last cacheline: 32 bytes */
};
struct NNObjString {
	NNObject                   objpadding;           /*     0    32 */
	uint32_t                   hash;                 /*    32     4 */

	/* XXX 4 bytes hole, try to pack */

	StringBuffer *             sbuf;                 /*    40     8 */

	/* size: 48, cachelines: 1, members: 3 */
	/* sum members: 44, holes: 1, sum holes: 4 */
	/* last cacheline: 48 bytes */
};
struct NNObjArray {
	NNObject                   objpadding;           /*     0    32 */
	NNValArray *               varray;               /*    32     8 */

	/* size: 40, cachelines: 1, members: 2 */
	/* last cacheline: 40 bytes */
};
struct NNObjUpvalue {
	NNObject                   objpadding;           /*     0    32 */
	int                        stackpos;             /*    32     4 */

	/* XXX 4 bytes hole, try to pack */

	NNValue                    closed;               /*    40     8 */
	NNValue                    location;             /*    48     8 */
	NNObjUpvalue *             next;                 /*    56     8 */

	/* size: 64, cachelines: 1, members: 5 */
	/* sum members: 60, holes: 1, sum holes: 4 */
};
struct NNObjClass {
	NNObject                   objpadding;           /*     0    32 */
	NNValue                    constructor;          /*    32     8 */
	NNValue                    destructor;           /*    40     8 */
	NNHashValTable *           instproperties;       /*    48     8 */
	NNHashValTable *           staticproperties;     /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	NNHashValTable *           instmethods;          /*    64     8 */
	NNHashValTable *           staticmethods;        /*    72     8 */
	NNObjString *              name;                 /*    80     8 */
	NNObjClass *               superclass;           /*    88     8 */

	/* size: 96, cachelines: 2, members: 9 */
	/* last cacheline: 32 bytes */
};
struct NNObjFuncNative {
	NNObject                   objpadding;           /*     0    32 */
	NNFuncType                 type;                 /*    32     4 */

	/* XXX 4 bytes hole, try to pack */

	const char  *              name;                 /*    40     8 */
	NNNativeFN                 natfunc;              /*    48     8 */
	void *                     userptr;              /*    56     8 */

	/* size: 64, cachelines: 1, members: 5 */
	/* sum members: 60, holes: 1, sum holes: 4 */
};
struct NNObjModule {
	NNObject                   objpadding;           /*     0    32 */
	_Bool                      imported;             /*    32     1 */

	/* XXX 7 bytes hole, try to pack */

	NNHashValTable *           deftable;             /*    40     8 */
	NNObjString *              name;                 /*    48     8 */
	NNObjString *              physicalpath;         /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	void *                     preloader;            /*    64     8 */
	void *                     unloader;             /*    72     8 */
	void *                     handle;               /*    80     8 */

	/* size: 88, cachelines: 2, members: 8 */
	/* sum members: 81, holes: 1, sum holes: 7 */
	/* last cacheline: 24 bytes */
};
struct NNObjFuncScript {
	NNObject                   objpadding;           /*     0    32 */
	NNFuncType                 type;                 /*    32     4 */
	int                        arity;                /*    36     4 */
	int                        upvalcount;           /*    40     4 */
	_Bool                      isvariadic;           /*    44     1 */

	/* XXX 3 bytes hole, try to pack */

	NNBlob                     blob;                 /*    48    40 */
	/* --- cacheline 1 boundary (64 bytes) was 24 bytes ago --- */
	NNObjString *              name;                 /*    88     8 */
	NNObjModule *              module;               /*    96     8 */

	/* size: 104, cachelines: 2, members: 8 */
	/* sum members: 101, holes: 1, sum holes: 3 */
	/* last cacheline: 40 bytes */
};
struct NNObjFuncClosure {
	NNObject                   objpadding;           /*     0    32 */
	int                        upvalcount;           /*    32     4 */

	/* XXX 4 bytes hole, try to pack */

	NNObjFuncScript *          scriptfunc;           /*    40     8 */
	NNObjUpvalue * *           upvalues;             /*    48     8 */

	/* size: 56, cachelines: 1, members: 4 */
	/* sum members: 52, holes: 1, sum holes: 4 */
	/* last cacheline: 56 bytes */
};
struct NNObjInstance {
	NNObject                   objpadding;           /*     0    32 */
	_Bool                      active;               /*    32     1 */

	/* XXX 7 bytes hole, try to pack */

	NNHashValTable *           properties;           /*    40     8 */
	NNObjClass *               klass;                /*    48     8 */

	/* size: 56, cachelines: 1, members: 4 */
	/* sum members: 49, holes: 1, sum holes: 7 */
	/* last cacheline: 56 bytes */
};
struct NNObjFuncBound {
	NNObject                   objpadding;           /*     0    32 */
	NNValue                    receiver;             /*    32     8 */
	NNObjFuncClosure *         method;               /*    40     8 */

	/* size: 48, cachelines: 1, members: 3 */
	/* last cacheline: 48 bytes */
};
struct NNObjRange {
	NNObject                   objpadding;           /*     0    32 */
	int                        lower;                /*    32     4 */
	int                        upper;                /*    36     4 */
	int                        range;                /*    40     4 */

	/* size: 48, cachelines: 1, members: 4 */
	/* padding: 4 */
	/* last cacheline: 48 bytes */
};
struct NNObjDict {
	NNObject                   objpadding;           /*     0    32 */
	NNValArray *               names;                /*    32     8 */
	NNHashValTable *           htab;                 /*    40     8 */

	/* size: 48, cachelines: 1, members: 3 */
	/* last cacheline: 48 bytes */
};
struct NNObjFile {
	NNObject                   objpadding;           /*     0    32 */
	_Bool                      isopen;               /*    32     1 */
	_Bool                      isstd;                /*    33     1 */
	_Bool                      istty;                /*    34     1 */

	/* XXX 1 byte hole, try to pack */

	int                        number;               /*    36     4 */
	FILE *                     handle;               /*    40     8 */
	NNObjString *              mode;                 /*    48     8 */
	NNObjString *              path;                 /*    56     8 */

	/* size: 64, cachelines: 1, members: 8 */
	/* sum members: 63, holes: 1, sum holes: 1 */
};
struct NNObjSwitch {
	NNObject                   objpadding;           /*     0    32 */
	int                        defaultjump;          /*    32     4 */
	int                        exitjump;             /*    36     4 */
	NNHashValTable *           table;                /*    40     8 */

	/* size: 48, cachelines: 1, members: 4 */
	/* last cacheline: 48 bytes */
};
struct NNObjUserdata {
	NNObject                   objpadding;           /*     0    32 */
	void *                     pointer;              /*    32     8 */
	char *                     name;                 /*    40     8 */
	NNPtrFreeFN                ondestroyfn;          /*    48     8 */

	/* size: 56, cachelines: 1, members: 4 */
	/* last cacheline: 56 bytes */
};
struct NNPropGetSet {
	NNValue                    getter;               /*     0     8 */
	NNValue                    setter;               /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};
struct NNProperty {
	_Bool                      havegetset;           /*     0     1 */

	/* XXX 3 bytes hole, try to pack */

	NNFieldType                type;                 /*     4     4 */
	NNValue                    value;                /*     8     8 */
	NNPropGetSet               getset;               /*    16    16 */

	/* size: 32, cachelines: 1, members: 4 */
	/* sum members: 29, holes: 1, sum holes: 3 */
	/* last cacheline: 32 bytes */
};
struct NNValArray {
	NNState *                  pvm;                  /*     0     8 */
	const char  *              listname;             /*     8     8 */
	NNValue *                  listitems;            /*    16     8 */
	size_t                     listcapacity;         /*    24     8 */
	size_t                     listcount;            /*    32     8 */

	/* size: 40, cachelines: 1, members: 5 */
	/* last cacheline: 40 bytes */
};
struct NNBlob {
	int                        count;                /*     0     4 */
	int                        capacity;             /*     4     4 */
	NNState *                  pvm;                  /*     8     8 */
	NNInstruction *            instrucs;             /*    16     8 */
	NNValArray *               constants;            /*    24     8 */
	NNValArray *               argdefvals;           /*    32     8 */

	/* size: 40, cachelines: 1, members: 6 */
	/* last cacheline: 40 bytes */
};
struct NNHashValEntry {
	NNValue                    key;                  /*     0     8 */
	NNProperty                 value;                /*     8    32 */

	/* size: 40, cachelines: 1, members: 2 */
	/* last cacheline: 40 bytes */
};
struct NNHashValTable {
	_Bool                      active;               /*     0     1 */

	/* XXX 3 bytes hole, try to pack */

	int                        count;                /*     4     4 */
	int                        capacity;             /*     8     4 */

	/* XXX 4 bytes hole, try to pack */

	NNState *                  pvm;                  /*    16     8 */
	NNHashValEntry *           entries;              /*    24     8 */

	/* size: 32, cachelines: 1, members: 5 */
	/* sum members: 25, holes: 2, sum holes: 7 */
	/* last cacheline: 32 bytes */
};
struct NNExceptionFrame {
	uint16_t                   address;              /*     0     2 */
	uint16_t                   finallyaddress;       /*     2     2 */

	/* XXX 4 bytes hole, try to pack */

	NNObjClass *               klass;                /*     8     8 */

	/* size: 16, cachelines: 1, members: 3 */
	/* sum members: 12, holes: 1, sum holes: 4 */
	/* last cacheline: 16 bytes */
};
struct NNCallFrame {
	int                        handlercount;         /*     0     4 */
	int                        gcprotcount;          /*     4     4 */
	int                        stackslotpos;         /*     8     4 */

	/* XXX 4 bytes hole, try to pack */

	NNInstruction *            inscode;              /*    16     8 */
	NNObjFuncClosure *         closure;              /*    24     8 */
	NNExceptionFrame           handlers[16];         /*    32   256 */

	/* size: 288, cachelines: 5, members: 6 */
	/* sum members: 284, holes: 1, sum holes: 4 */
	/* last cacheline: 32 bytes */
};
struct NNAstToken {
	_Bool                      isglobal;             /*     0     1 */

	/* XXX 3 bytes hole, try to pack */

	NNAstTokType               type;                 /*     4     4 */
	const char  *              start;                /*     8     8 */
	int                        length;               /*    16     4 */
	int                        line;                 /*    20     4 */

	/* size: 24, cachelines: 1, members: 5 */
	/* sum members: 21, holes: 1, sum holes: 3 */
	/* last cacheline: 24 bytes */
};
struct NNAstLexer {
	NNState *                  pvm;                  /*     0     8 */
	const char  *              start;                /*     8     8 */
	const char  *              sourceptr;            /*    16     8 */
	int                        line;                 /*    24     4 */
	int                        tplstringcount;       /*    28     4 */
	int                        tplstringbuffer[8];   /*    32    32 */

	/* size: 64, cachelines: 1, members: 6 */
};
struct NNAstLocal {
	_Bool                      iscaptured;           /*     0     1 */

	/* XXX 3 bytes hole, try to pack */

	int                        depth;                /*     4     4 */
	NNAstToken                 name;                 /*     8    24 */

	/* size: 32, cachelines: 1, members: 3 */
	/* sum members: 29, holes: 1, sum holes: 3 */
	/* last cacheline: 32 bytes */
};
struct NNAstUpvalue {
	_Bool                      islocal;              /*     0     1 */

	/* XXX 1 byte hole, try to pack */

	uint16_t                   index;                /*     2     2 */

	/* size: 4, cachelines: 1, members: 2 */
	/* sum members: 3, holes: 1, sum holes: 1 */
	/* last cacheline: 4 bytes */
};
struct NNAstClassCompiler {
	_Bool                      hassuperclass;        /*     0     1 */

	/* XXX 7 bytes hole, try to pack */

	NNAstClassCompiler *       enclosing;            /*     8     8 */
	NNAstToken                 name;                 /*    16    24 */

	/* size: 40, cachelines: 1, members: 3 */
	/* sum members: 33, holes: 1, sum holes: 7 */
	/* last cacheline: 40 bytes */
};
struct NNAstParser {
	_Bool                      haderror;             /*     0     1 */
	_Bool                      panicmode;            /*     1     1 */
	_Bool                      isreturning;          /*     2     1 */
	_Bool                      istrying;             /*     3     1 */
	_Bool                      replcanecho;          /*     4     1 */
	_Bool                      keeplastvalue;        /*     5     1 */
	_Bool                      lastwasstatement;     /*     6     1 */
	_Bool                      infunction;           /*     7     1 */
	_Bool                      inswitch;             /*     8     1 */
	_Bool                      stopprintingsyntaxerrors; /*     9     1 */

	/* XXX 2 bytes hole, try to pack */

	int                        innermostloopstart;   /*    12     4 */
	int                        innermostloopscopedepth; /*    16     4 */
	int                        blockcount;           /*    20     4 */
	int                        errorcount;           /*    24     4 */
	NNAstCompContext           compcontext;          /*    28     4 */
	const char  *              currentfile;          /*    32     8 */
	NNState *                  pvm;                  /*    40     8 */
	NNAstLexer *               lexer;                /*    48     8 */
	NNAstToken                 currtoken;            /*    56    24 */
	/* --- cacheline 1 boundary (64 bytes) was 16 bytes ago --- */
	NNAstToken                 prevtoken;            /*    80    24 */
	NNAstFuncCompiler *        currentfunccompiler;  /*   104     8 */
	NNAstClassCompiler *       currentclasscompiler; /*   112     8 */
	NNObjModule *              currentmodule;        /*   120     8 */

	/* size: 128, cachelines: 2, members: 23 */
	/* sum members: 126, holes: 1, sum holes: 2 */
};
struct NNAstRule {
	NNAstParsePrefixFN         prefix;               /*     0     8 */
	NNAstParseInfixFN          infix;                /*     8     8 */
	NNAstPrecedence            precedence;           /*    16     4 */

	/* size: 24, cachelines: 1, members: 3 */
	/* padding: 4 */
	/* last cacheline: 24 bytes */
};
struct NNRegFunc {
	const char  *              name;                 /*     0     8 */
	_Bool                      isstatic;             /*     8     1 */

	/* XXX 7 bytes hole, try to pack */

	NNNativeFN                 function;             /*    16     8 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 17, holes: 1, sum holes: 7 */
	/* last cacheline: 24 bytes */
};
struct NNRegField {
	const char  *              name;                 /*     0     8 */
	_Bool                      isstatic;             /*     8     1 */

	/* XXX 7 bytes hole, try to pack */

	NNClassFieldFN             fieldvalfn;           /*    16     8 */

	/* size: 24, cachelines: 1, members: 3 */
	/* sum members: 17, holes: 1, sum holes: 7 */
	/* last cacheline: 24 bytes */
};
struct NNRegClass {
	const char  *              name;                 /*     0     8 */
	NNRegField *               fields;               /*     8     8 */
	NNRegFunc *                functions;            /*    16     8 */

	/* size: 24, cachelines: 1, members: 3 */
	/* last cacheline: 24 bytes */
};
struct NNRegModule {
	const char  *              name;                 /*     0     8 */
	NNRegField *               fields;               /*     8     8 */
	NNRegFunc *                functions;            /*    16     8 */
	NNRegClass *               classes;              /*    24     8 */
	NNModLoaderFN              preloader;            /*    32     8 */
	NNModLoaderFN              unloader;             /*    40     8 */

	/* size: 48, cachelines: 1, members: 6 */
	/* last cacheline: 48 bytes */
};
struct NNState {
	struct {
		_Bool              enablewarnings;       /*     0     1 */
		_Bool              dumpbytecode;         /*     1     1 */
		_Bool              exitafterbytecode;    /*     2     1 */
		_Bool              shoulddumpstack;      /*     3     1 */
		_Bool              enablestrictmode;     /*     4     1 */
		_Bool              showfullstack;        /*     5     1 */
		_Bool              enableapidebug;       /*     6     1 */
		_Bool              enableastdebug;       /*     7     1 */
		int                maxsyntaxerrors;      /*     8     4 */
	} conf;                                          /*     0    12 */

	/* XXX 4 bytes hole, try to pack */

	struct {
		size_t             stackidx;             /*    16     8 */
		size_t             stackcapacity;        /*    24     8 */
		size_t             framecapacity;        /*    32     8 */
		size_t             framecount;           /*    40     8 */
		NNInstruction      currentinstr;         /*    48     8 */
		NNCallFrame *      currentframe;         /*    56     8 */
		/* --- cacheline 1 boundary (64 bytes) --- */
		NNObjUpvalue *     openupvalues;         /*    64     8 */
		NNObject *         linkedobjects;        /*    72     8 */
		NNCallFrame *      framevalues;          /*    80     8 */
		NNValue *          stackvalues;          /*    88     8 */
	} vmstate;                                       /*    16    80 */
	struct {
		int                graycount;            /*    96     4 */
		int                graycapacity;         /*   100     4 */
		int                bytesallocated;       /*   104     4 */
		int                nextgc;               /*   108     4 */
		NNObject * *       graystack;            /*   112     8 */
	} gcstate;                                       /*    96    24 */
	struct {
		NNObjClass *       stdexception;         /*   120     8 */
		/* --- cacheline 2 boundary (128 bytes) --- */
		NNObjClass *       syntaxerror;          /*   128     8 */
		NNObjClass *       asserterror;          /*   136     8 */
		NNObjClass *       ioerror;              /*   144     8 */
		NNObjClass *       oserror;              /*   152     8 */
		NNObjClass *       argumenterror;        /*   160     8 */
	} exceptions;                                    /*   120    48 */
	void *                     memuserptr;           /*   168     8 */
	const char  *              rootphysfile;         /*   176     8 */
	NNObjDict *                envdict;              /*   184     8 */
	/* --- cacheline 3 boundary (192 bytes) --- */
	NNObjString *              constructorname;      /*   192     8 */
	NNObjModule *              topmodule;            /*   200     8 */
	NNValArray *               importpath;           /*   208     8 */
	NNHashValTable *           modules;              /*   216     8 */
	NNHashValTable *           allocatedstrings;     /*   224     8 */
	NNHashValTable *           conststrings;         /*   232     8 */
	NNHashValTable *           globals;              /*   240     8 */
	NNObjClass *               classprimprocess;     /*   248     8 */
	/* --- cacheline 4 boundary (256 bytes) --- */
	NNObjClass *               classprimobject;      /*   256     8 */
	NNObjClass *               classprimnumber;      /*   264     8 */
	NNObjClass *               classprimstring;      /*   272     8 */
	NNObjClass *               classprimarray;       /*   280     8 */
	NNObjClass *               classprimdict;        /*   288     8 */
	NNObjClass *               classprimfile;        /*   296     8 */
	NNObjClass *               classprimrange;       /*   304     8 */
	NNObjClass *               classprimmath;        /*   312     8 */
	/* --- cacheline 5 boundary (320 bytes) --- */
	NNObjClass *               classprimcallable;    /*   320     8 */
	_Bool                      isrepl;               /*   328     1 */
	_Bool                      markvalue;            /*   329     1 */

	/* XXX 6 bytes hole, try to pack */

	NNProcessInfo *            processinfo;          /*   336     8 */
	NNPrinter *                stdoutprinter;        /*   344     8 */
	NNPrinter *                stderrprinter;        /*   352     8 */
	NNPrinter *                debugwriter;          /*   360     8 */

	/* size: 368, cachelines: 6, members: 30 */
	/* sum members: 358, holes: 2, sum holes: 10 */
	/* last cacheline: 48 bytes */
};
struct NNPrinter {
	_Bool                      shouldclose;          /*     0     1 */
	_Bool                      shouldflush;          /*     1     1 */
	_Bool                      stringtaken;          /*     2     1 */
	_Bool                      fromstack;            /*     3     1 */
	_Bool                      shortenvalues;        /*     4     1 */

	/* XXX 3 bytes hole, try to pack */

	size_t                     maxvallength;         /*     8     8 */
	NNPrMode                   wrmode;               /*    16     4 */

	/* XXX 4 bytes hole, try to pack */

	NNState *                  pvm;                  /*    24     8 */
	StringBuffer *             strbuf;               /*    32     8 */
	FILE *                     handle;               /*    40     8 */

	/* size: 48, cachelines: 1, members: 10 */
	/* sum members: 41, holes: 2, sum holes: 7 */
	/* last cacheline: 48 bytes */
};
struct NNArgCheck {
	NNState *                  pvm;                  /*     0     8 */
	const char  *              name;                 /*     8     8 */
	int                        argc;                 /*    16     4 */

	/* XXX 4 bytes hole, try to pack */

	NNValue *                  argv;                 /*    24     8 */

	/* size: 32, cachelines: 1, members: 4 */
	/* sum members: 28, holes: 1, sum holes: 4 */
	/* last cacheline: 32 bytes */
};
struct NNArguments {
	size_t                     count;                /*     0     8 */
	NNValue *                  args;                 /*     8     8 */
	NNValue                    thisval;              /*    16     8 */
	const char  *              name;                 /*    24     8 */
	void *                     userptr;              /*    32     8 */

	/* size: 40, cachelines: 1, members: 5 */
	/* last cacheline: 40 bytes */
};
struct NNInstruction {
	_Bool                      isop;                 /*     0     1 */
	uint8_t                    code;                 /*     1     1 */

	/* XXX 2 bytes hole, try to pack */

	int                        srcline;              /*     4     4 */

	/* size: 8, cachelines: 1, members: 3 */
	/* sum members: 6, holes: 1, sum holes: 2 */
	/* last cacheline: 8 bytes */
};
struct utf8iterator_t {
	const char  *              plainstr;             /*     0     8 */
	uint32_t                   plainlen;             /*     8     4 */
	uint32_t                   codepoint;            /*    12     4 */
	uint8_t                    charsize;             /*    16     1 */

	/* XXX 3 bytes hole, try to pack */

	uint32_t                   currpos;              /*    20     4 */
	uint32_t                   nextpos;              /*    24     4 */
	uint32_t                   currcount;            /*    28     4 */

	/* size: 32, cachelines: 1, members: 7 */
	/* sum members: 29, holes: 1, sum holes: 3 */
	/* last cacheline: 32 bytes */
};
struct NNHashPtrTable {
	NNState *                  pstate;               /*     0     8 */
	size_t                     keytypesize;          /*     8     8 */
	size_t                     valtypesize;          /*    16     8 */
	unsigned int *             vdcells;              /*    24     8 */
	long unsigned int *        vdhashes;             /*    32     8 */
	char * *                   vdkeys;               /*    40     8 */
	void * *                   vdvalues;             /*    48     8 */
	unsigned int *             vdcellindices;        /*    56     8 */
	/* --- cacheline 1 boundary (64 bytes) --- */
	unsigned int               vdcount;              /*    64     4 */
	unsigned int               vditemcapacity;       /*    68     4 */
	unsigned int               vdcellcapacity;       /*    72     4 */

	/* XXX 4 bytes hole, try to pack */

	mcitemhashfn_t             funchashfn;           /*    80     8 */
	mcitemcomparefn_t          funckeyequalsfn;      /*    88     8 */

	/* size: 96, cachelines: 2, members: 13 */
	/* sum members: 92, holes: 1, sum holes: 4 */
	/* last cacheline: 32 bytes */
};
union NNUtilDblUnion {
	uint64_t                   bits;               /*     0     8 */
	double                     num;                /*     0     8 */
};
struct ClsListMethods {
	const char  *              name;                 /*     0     8 */
	NNNativeFN                 fn;                   /*     8     8 */

	/* size: 16, cachelines: 1, members: 2 */
	/* last cacheline: 16 bytes */
};
struct dirent {
	__ino_t                    d_ino;                /*     0     8 */
	__off_t                    d_off;                /*     8     8 */
	short unsigned int         d_reclen;             /*    16     2 */
	unsigned char              d_type;               /*    18     1 */
	char                       d_name[256];          /*    19   256 */

	/* size: 280, cachelines: 5, members: 5 */
	/* padding: 5 */
	/* last cacheline: 24 bytes */
};
