#define SUBSTRIPES 3
#define SUBSTRIPE_DATA 5
#define E_BLOCKS 1

const unsigned char lrc_scheme[(SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + 1] =
{0x2, 0x1, 0x2, 0x1, 0x0, 0x2, 0x1, 0x1, 0x2, 0x2, 0xc0, 0xc1, 0xc2, 0x1, 0xee, 0x0, 0x0, 0x0, 0x0, 0xff};

// it is just lrc_scheme without 0xee, 0xff and 0xcN
const unsigned char lrc_data[SUBSTRIPE_DATA * SUBSTRIPES] =
{0x2, 0x1, 0x2, 0x1, 0x0, 0x2, 0x1, 0x1, 0x2, 0x2, 0x1, 0x0, 0x0, 0x0, 0x0};

// it is place of global syndrome
const int lrc_gs =19;
// places of all local syndromes
const int lrc_ls[SUBSTRIPES] = {10,11,12};
// empty place
const int lrc_eb =14;
// not-data blocks, ordered by increasing
const int lrc_offset[SUBSTRIPES + E_BLOCKS + 1] = {10,11,12,14,19};
// number of the last data block
const int lrc_ldb =18;

