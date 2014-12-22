#!/bin/python

def print_array(array):
    i = 0
    
    print('{',end='')
    while i < len(array):
        print(array[i],end='')
        i += 1
        if i < len(array):
            print(', ',end='')
    print('};')

def get_hex_scheme(scheme):
    i = 0
    syndromes = 0;
    array = []
    while i < len(scheme):
        if scheme[i].isdigit():
            array.append(hex(int(scheme[i]) - 1))
        else:
            if ((scheme[i] == 's') or (scheme[i] == 'S')):
                array.append(hex(192 + syndromes))
                syndromes += 1
            elif ((scheme[i] == 'e') or (scheme[i] == 'E')):
                array.append(hex(0xee))
            else:
                array.append(hex(0xff))
        i += 1
    return array

def get_data_scheme(hex_scheme):
    i = 0
    array = []
    while i < len(hex_scheme):
        fs = hex_scheme[i][2]
        if ((fs != 'c') and (fs != 'e') and (fs != 'f')):
            array.append(hex_scheme[i])
        i += 1
    return array

def get_ls_places(hex_scheme):
    i = 0
    array = []
    while i < len(hex_scheme):
        if (hex_scheme[i][2] == 'c'):
            array.append(i)
        i += 1
    return array

def ordered_offset(hex_scheme):
    array = [];
    array[0:0] = (get_ls_places(hex_scheme))
    array.append(hex_scheme.index(hex(0xee)))
    array.append(hex_scheme.index(hex(0xff)))
    array.sort()
    return array

def get_ldb(hex_scheme):
    i = len(hex_scheme) - 1
    while i >= 0:
        fs = hex_scheme[i][2]
        if ((fs != 'c') and (fs != 'e') and (fs !='f')):
            break;
        i -= 1
    return i

def print_all_this_shit(scheme):
    print('')
    print('')
    print('const unsigned char lrc_scheme[(SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + 1] =')
    hex_scheme = get_hex_scheme(scheme)
    print_array(hex_scheme)
    print('')

    print('// it is just lrc_scheme without 0xee, 0xff and 0xcN')
    print('const unsigned char lrc_data[SUBSTRIPE_DATA * SUBSTRIPES] =')
    data_scheme = get_data_scheme(hex_scheme)
    print_array(data_scheme)
    print('')

    print('// it is place of global syndrome')
    print('const int lrc_gs =',hex_scheme.index(hex(0xff)))

    print('// places of all local syndromes')
    print('const int lrc_ls[SUBSTRIPES] = {',end='')
    ls = get_ls_places(hex_scheme)
    i = 0
    while i < len(ls):
        print(ls[i],end='')
        i += 1
        if i < len(ls):
            print(',',end='')
    print('};')
    
    print('// empty place')
    print('const int lrc_eb =',hex_scheme.index(hex(0xee)))

    print('// not-data blocks, ordered by increasing')
    print('const int lrc_offset[SUBSTRIPES + E_BLOCKS + 1] = {',end='')
    oo = ordered_offset(hex_scheme)
    i = 0
    while i < len(oo):
        print(oo[i],end='')
        i += 1
        if i < len(oo):
            print(',',end='')
    print('};')
    
    print('// number of the last data block')
    print('const int lrc_ldb =',get_ldb(hex_scheme),end='')
    print(';')

def get_scheme():
    print('You can input numbers 1-9, letters "s","e","g" (or "S","E","G")')
    scheme = input('Input the scheme: ')
    return scheme

def main():
    scheme = get_scheme()
    print_all_this_shit(scheme)

main()
