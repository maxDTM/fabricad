import json
key=json.load(open('key.json'))
same={'b02':['error'],'b03':['Y','Z','X'],'b04':['warning','error'],'b05':['live'],'b06':['X','Y','Z'],'b07':['Y','Z','X'],'b08':['warning'],'b09':['selection','free'],'b10':['error'],'b11':['warning','error'],'b12':['X','Y','Z'],'b13':['warning'],'b14':['warning'],'b15':['error'],'b16':['error'],'b17':['live'],'b18':['live'],'b19':['error'],'b20':['live'],'b21':['warning'],'b22':['selection','free'],'b23':['warning'],'b24':['live'],'b26':['live'],'b27':['error'],'b29':['error'],'b30':['error'],'b31':['live'],'b33':['live'],'b34':['warning']}
labels={k:[v]*4 for k,v in same.items()}
for k in ('b28','b32'): labels[k]=[['error'],['error'],['warning'],['error']]
json.dump(labels,open('labels.json','w'))
tot=ok=0; lw=lwok=0; miss=[]; byname={}
for k,views in labels.items():
    t=key[k]['truth']
    for vi,v in enumerate(views):
        for a,b in zip(v,t):
            tot+=1; good=a==b; ok+=good
            if b in('live','warning'):
                lw+=1; lwok+=good
            if not good: miss.append((k,key[k]['name'],key[k]['theme'],'NPDT'[vi],a,b))
print(tot,ok,ok/tot,lw,lwok)
print(miss)
for k in labels: print(k,key[k]['theme'],key[k]['name'],key[k]['truth'])
print(key['b01'],key['b25'])
