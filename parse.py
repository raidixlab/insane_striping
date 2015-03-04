#!/usr/bin/python
# -*- coding: UTF-8 -*-
"""This script automates testing of insane_striping algorithms.
You should fill configuration file with your own values,then start this script
as 'python parse.py <config>'. If you test LRC, make sure that stripe-searcher
is in './searcher' catalogue.
Results of tests will be contained in the file "results.csv"
"""
__author__ = "Mariya Podpirova, Evgeny Anastasiev"
__copyright__ = "Copyright (C) 2015, Raidix"
__credits__ = ["Mariya Podpirova","Evgeny Anastasiev"]
__license__ = "GPL"
__version__ = "1.0"
__maintainer__ = "Evgeny Anastasiev"
__status__ = "Production"


import subprocess
import os

pathtobrute = 'searcher'

def read_section(section):
    lst = open('pattern2','r').readlines()
    result = []
    for line in lst:
        if line[0] == '[':
            i = line.strip()[1:-1]
        if i == section:
            if (line[0] != '#') and (line.strip() != '') and (line[0] != '['):
                result.append(line.strip())
    return result

devices = read_section('devices')
volume = read_section('volume')
block_sizes = read_section('block_sizes')
tests = read_section('tests')
partitions = devices[0].split(' ')
size_disk = int(volume[0][:-1])
byte = (volume[0][-1])
sizes = block_sizes[0].split(' ')

################################################################
schemes = 'schemes.csv'
columns = {'groups': 0, 'length': 1, 'disks': 2, 'global_s': 3, 'scheme': 4}

def dict2list(dct):
    lst = ['']*len(dct)

    for i in dct.items():
        lst[columns[i[0]]] = i[1]

    return lst

def add_scheme(dct):
    lst = dict2list(dct)
    schm = ','.join(str(i) for i in lst) + '\n'

    with open(schemes, "a") as f:
        f.write(schm)

    return 0

def search_scheme(params):
    f = open(schemes)

    for line in f:
        array = line.split(',')
        match = True
        for j in params.items():
            try:
                pattern = int(array[columns[j[0]]])
            except:
                pattern = 0
            if (pattern != j[1]):
                match = False

        if match:
            return array[columns['scheme']].strip()

    return 0
################################################################

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



    # Эта функция формирует основной массив строк файла lrc_config.c
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
###############################################################
line1 = ','.join(sizes)
with open('results.csv', 'a') as first:
    first.write(',%s\n' % (line1))

for i in xrange(len(tests)):
    test1 = tests[i].split(' ')
    disks_count = int(test1[0])

    if len(partitions) < disks_count:
        print 'Error, partitions < disks'
        exit()

    if len(test1) > 2:
        if test1[2][:6] == 'scheme':
            get_scheme = test1[2][7:]

        if test1[2][:6] == 'groups':
            groups = int(test1[2][7:])
            length = int(test1[3][7:])
            if len(test1) > 4:
                global_s = int(test1[4][9:])
            else:
                global_s = 1
     
            param = {'groups': groups, 'length': length, 'disks': disks_count, 'global_s': global_s}
            if search_scheme(param) == 0:
                with open(os.path.join('%s/' % (pathtobrute), 'defines.c'),'w') as defnes:
                    defnes.write('#define disks_count %s\n#define groups_count %s\n#define group_len %s\n' % (disks_count, groups, length))
                command1 ="cd %s; make all;timeout -s INT 20 ./main 1 1 > tmp.res ; cat tmp.res | grep G | sed -e 's/^.*[\t]//g; s/[ \t]*//g; q' | tr -d '\n'; rm -f tmp.res" % (pathtobrute)
                process1 = subprocess.Popen(command1, stdout=subprocess.PIPE, shell=True)
                out = process1.stdout.read()
                # запуск искалки
                # поиск схемы
                dct={'groups': groups, 'length': length, 'disks': disks_count, 'global_s': global_s, 'scheme': out} 
                add_scheme(dct)
                get_scheme = out
                # get_scheme = #схема из искалки
            else:
                get_scheme = search_scheme(param)

        new_info = defines(get_scheme)
        new_info += constants(get_scheme)
        with open('lrc_config.c', 'w') as f:
            f.writelines(new_info)
    else:
        get_scheme = ''

    parts = []
    for j in xrange(disks_count):
        parts.append(partitions[j])
    disks = ' '.join(parts)

    if byte == 'G'or byte == 'g':
        size_block = disks_count*2*1024*1024*size_disk
    if byte == 'M' or byte == 'm':
        size_block = disks_count*2*1024*size_disk
    if byte == 'K' or byte == 'k':
        size_block = disks_count*2*size_disk
    array_speed = []
    for k in xrange(len(sizes)):
        with open('TABLE', 'w') as table:
            table.write('0 %s insane %s %s %s recover 1 %s\n' % (size_block, test1[1], disks_count, int(sizes[k]), disks))#+размер блоков и тд

        # запуск bash скриптов
        subprocess.call('./run_up')

        command3 = './results'
        process3 = subprocess.Popen(command3, stdout=subprocess.PIPE, shell=True)
        out3 = process3.stdout.read()
        # with open('restable','a') as rest:
        #     rest.write('%s %s %s %s %s\n' % (int(sizes[k]), test1[1], disks_count,get_scheme, out3))

        subprocess.call('./clean')

        array_speed.append(out3)

    speed = ','.join(array_speed)
    with open('results.csv', 'a') as res:
        res.write('%s %s %s,%s\n' % (test1[1], disks_count, get_scheme, speed))
