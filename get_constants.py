#!/bin/python

def defines(scheme):
    i = 0
    sd = 0
    ss = 0
    eb = 0
    while i < len(scheme):
        if (scheme[i] == '1'):
            sd += 1
        if (scheme[i].isdigit()):
            if int(scheme[i]) > ss:
                ss += 1
        if ((scheme[i] == 'e') or (scheme[i] == 'E')):
            eb += 1
        i += 1
    
    dfns = []

    dfns.append('#define SUBSTRIPES ' + str(ss) + '\n')
    dfns.append('#define SUBSTRIPE_DATA ' + str(sd) + '\n')
    dfns.append('#define E_BLOCKS ' + str(eb) + '\n')

    return dfns


def print_array(array):
    i = 0
    
    st = '{'
    while i < len(array):
        st += str(array[i])
        i += 1
        if i < len(array):
            st += ', '
    st += '};\n'
    return st

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

def constants(scheme):
    cnstns = []
    cnstns.append('\n')
    cnstns.append('const unsigned char lrc_scheme[(SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + 1] =\n')
    hex_scheme = get_hex_scheme(scheme)
    cnstns.append(print_array(hex_scheme))
    cnstns.append('\n')

    cnstns.append('// it is just lrc_scheme without 0xee, 0xff and 0xcN\n')
    cnstns.append('const unsigned char lrc_data[SUBSTRIPE_DATA * SUBSTRIPES] =\n')
    data_scheme = get_data_scheme(hex_scheme)
    cnstns.append(print_array(data_scheme))
    cnstns.append('\n')

    cnstns.append('// it is place of global syndrome\n')
    cnstns.append('const int lrc_gs =' + str(hex_scheme.index(hex(0xff))) + ';\n')

    cnstns.append('// places of all local syndromes\n')

    st = ''
    st += 'const int lrc_ls[SUBSTRIPES] = {'
    ls = get_ls_places(hex_scheme)
    i = 0
    while i < len(ls):
        st += str(ls[i])
        i += 1
        if i < len(ls):
            st += ','
    st += '};\n'
    cnstns.append(st)
    
    cnstns.append('// empty place\n')
    cnstns.append('const int lrc_eb =' + str(hex_scheme.index(hex(0xee))) + ';\n')

    cnstns.append('// not-data blocks, ordered by increasing\n')
    st = ''
    st += 'const int lrc_offset[SUBSTRIPES + E_BLOCKS + 1] = {'
    oo = ordered_offset(hex_scheme)
    i = 0
    while i < len(oo):
        st += str(oo[i])
        i += 1
        if i < len(oo):
            st += ','
    st += '};\n'
    cnstns.append(st)
    
    cnstns.append('// number of the last data block\n')
    cnstns.append('const int lrc_ldb =' + str(get_ldb(hex_scheme)) + ';\n')
    cnstns.append('\n')

    return cnstns

def get_scheme():
    print('You can input numbers 1-9, letters "s","e","g" (or "S","E","G")')
    scheme = input('Input the scheme: ')
    return scheme

def make_file(scheme):
    new_info = []
    new_info += defines(scheme)
    new_info += constants(scheme)
    with open('lrc_config.c', 'w') as file:
        file.writelines(new_info)
    

def main():
    scheme = get_scheme()
    make_file(scheme)

main()
