import numpy as np
if __name__ == '__main__':
    f = open('codesize.data')

    lineno = 0
    values = []
    for line in f:
        num = line.split()[0]
        if lineno % 3 == 0:
            prog = line.split('/')[-1]
        elif lineno % 3 == 1:
            noidem = num
        else:
            idem = num
            values.append((prog, noidem, idem))
        lineno += 1

    diff_size= []
    increase = []
    for prog in values:
        #print '{0}: {1} {2} {3}%'.format(prog[0], prog[1], prog[2], float(prog[1])/float(prog[2])*100-100)
        diff_size.append(int(prog[1]) - int(prog[2]))
        increase.append(float(prog[1])/float(prog[2])*100-100)

    print 'Average diff={}'.format(np.mean(diff_size))
    print 'Average incr={}'.format(np.mean(increase))

    f.close()
