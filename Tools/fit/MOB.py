import sys

s = sys.argv[1].split(';')[1:-1]

#print s

o = []

for p in range(6):
    o.append('{ ')
    for k in range(15):
        if k != 14:
            o[p] += '{{{0:>3},{1:>3}}}, '.format(s[k+p*15],s[6*15+k+p*15])
        else:
            o[p] += '{{{0:>3},{1:>3}}}'.format(s[k+p*15],s[6*15+k+p*15])
    if p != 5:
        o[p]+=(' },')
    else:
        o[p]+=(' }')
    print(o[p])
