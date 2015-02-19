#!/bin/python

# Эта функция формирует все define'ы для файла lrc_config.c
def defines(scheme):
    sd = scheme.count('1') - 1
    ss = scheme.count('s') + scheme.count('S')
    eb = scheme.count('e') + scheme.count('E')
    gs = scheme.count('g') + scheme.count('G')

    dfns = []

    dfns.append('#define SUBSTRIPES ' + str(ss) + '\n')
    dfns.append('#define SUBSTRIPE_DATA ' + str(sd) + '\n')
    dfns.append('#define E_BLOCKS ' + str(eb) + '\n')
    dfns.append('#define GLOBAL_S ' + str(gs) + '\n')

    return dfns

# Вспомогательная функция вывода массива в годном для C виде
def print_array(array):
    st = '{'

    for i in array: 
        st += str(i)
        st += ', '
    
    st = st[:-2] + '};\n' # Стираем последнюю запятую и добавляем фигурную скобку
    return st

# Эта функция переводит введенную схему в 
# схему, понятную сишному модулю.

def get_hex_scheme(scheme):
    i = 0
    array = []

    # Здесь такой дурной цикл по той причине, что в локальном
    # синдроме требуется обрабатывать два символа за раз
    while i < len(scheme):
        if scheme[i].isdigit():                                 # Блок данных
            array.append(hex(int(scheme[i]) - 1))
        else:
            if ((scheme[i] == 's') or (scheme[i] == 'S')):      # Локальный синдром
                array.append(hex(191 + int(scheme[i+1])))
                i += 1
            elif ((scheme[i] == 'e') or (scheme[i] == 'E')):    # Empty-block
                array.append(hex(0xee))
            else:                                               # Глобальный синдром
                array.append(hex(0xff))
        i += 1
    return array

# Следующие 5 функций нужны специально для того,
# чтобы формировать некоторые переменные для файла.

def get_data_scheme(hex_scheme):
    array = []

    for i in hex_scheme:
        fs = i[2]
        if ((fs != 'c') and (fs != 'e') and (fs != 'f')):
            array.append(i)
    return array

def get_ls_places(hex_scheme):
    i = 0
    array = []
    while i < len(hex_scheme):
        if (hex_scheme[i][2] == 'c'):
            array.append(i)
        i += 1
    return array

def get_gs(hex_scheme):
    i = 0
    array = []
    while i < len(hex_scheme):
        if(hex_scheme[i][2] == 'f'):
            array.append(i)
        i += 1
    return array

def ordered_offset(hex_scheme):
    array = []
    array[0:0] = (get_ls_places(hex_scheme))    # Сперва получим локальные синдромы
    array[1:1] = (get_gs(hex_scheme))           # Потом глобальные
    array.append(hex_scheme.index(hex(0xee)))   # А потом empty-block

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

        

# Эта функция формирует основной массив строк файла lrc_config.c, ее можно не трогать
def constants(scheme):
    cnstns = []
    cnstns.append('\n')
    cnstns.append('const unsigned char lrc_scheme[(SUBSTRIPE_DATA + 1) * SUBSTRIPES + E_BLOCKS + GLOBAL_S] =\n')
    hex_scheme = get_hex_scheme(scheme)
    cnstns.append(print_array(hex_scheme))
    cnstns.append('\n')

    cnstns.append('// it is just lrc_scheme without 0xee, 0xff and 0xcN\n')
    cnstns.append('const unsigned char lrc_data[SUBSTRIPE_DATA * SUBSTRIPES] =\n')
    data_scheme = get_data_scheme(hex_scheme)
    cnstns.append(print_array(data_scheme))
    cnstns.append('\n')

    cnstns.append('// it is place of global syndrome\n')
    gs_array = get_gs(hex_scheme)
    cnstns.append('const int lrc_gs[GLOBAL_S] = ')
    cnstns.append(print_array(gs_array))
    cnstns.append('\n')

    cnstns.append('// places of all local syndromes\n')
    cnstns.append('const int lrc_ls[SUBSTRIPES] = ')
    ls = get_ls_places(hex_scheme)
    cnstns.append(print_array(ls))
    
    cnstns.append('// empty place\n')
    cnstns.append('const int lrc_eb = ' + str(hex_scheme.index(hex(0xee))) + ';\n')

    cnstns.append('// not-data blocks, ordered by increasing\n')
    cnstns.append('const int lrc_offset[SUBSTRIPES + E_BLOCKS + GLOBAL_S] = ')
    oo = ordered_offset(hex_scheme)
    cnstns.append(print_array(oo))
    
    cnstns.append('// number of the last data block\n')
    cnstns.append('const int lrc_ldb = ' + str(get_ldb(hex_scheme)) + ';\n')
    cnstns.append('\n')

    return cnstns

# Вот эту штуку надо бы переписать, чтобы схема задавалась не input'ом.
def get_scheme():
    print('You can input numbers 1-9, letters "s","e","g" (or "S","E","G")')
    scheme = input('Input the scheme: ')
    return scheme


def make_file(scheme):
    new_info = defines(scheme)
    new_info += constants(scheme)
    with open('lrc_config.c', 'w') as file:
        file.writelines(new_info)
    

def main():
    scheme = get_scheme()
    make_file(scheme)

main()
