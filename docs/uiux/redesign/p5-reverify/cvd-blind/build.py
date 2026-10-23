import json, random, subprocess, os, shutil
W=os.environ.get('CVD_WORK','/tmp/cvd-blind')  # scratch dir with the raw grabs
R=W+'/raw'
CVD=os.path.join(os.path.dirname(os.path.abspath(__file__)),'../../../../../tools/uitest/cvd.py')
# name, raw prefix, box, marks (description), truth list
C=[
('live-row','off-{t}-live',(0,204,320,30),['the Extrude1 row'],['live']),
('live-chip','off-{t}-live',(445,122,152,28),['the EDITING chip'],['live']),
('dist-tag','gui-{t}-live',(1005,140,175,62),['the DIST tag and field'],['live']),
('preview','gui-{t}-live',(740,240,480,250),['the translucent solid and its handle'],['live']),
('warn-row','off-{t}-zoo2',(0,176,320,28),['the Sketch1 row'],['warning']),
('warn-tree','off-{t}-zoo2',(40,722,260,30),['the Sketch1 row'],['warning']),
('warn-active','gui-{t}-skz',(0,176,320,28),['the Sketch1 row'],['warning']),
('err-row','off-{t}-zoo2',(0,204,320,28),['the Extrude1 row'],['error']),
('err-card','off-{t}-zero',(1296,578,340,28),['the message line'],['error']),
('err-status','off-{t}-zoo2',(545,1054,540,26),['the status message'],['error']),
('err-cell','off-{t}-skz',(1225,1054,215,26),['the SKETCH1 status cell'],['error']),
('conflict','gui-{t}-skz',(850,330,70,150),['the thick outlined rectangle'],['error']),
('rows-both','off-{t}-zoo2',(0,176,320,56),['top row','bottom row'],['warning','error']),
('sel-free','gui-{t}-sel',(505,150,400,160),['top edge of rectangle','circle outline'],['selection','free']),
('dial','off-{t}-live',(1720,120,140,150),['top square','centre square','right square'],['Y','Z','X']),
('readout','off-{t}-live',(342,1010,320,28),['left marker','middle marker','right marker'],['X','Y','Z']),
('origin-axes','gui-{t}-sel',(780,200,170,110),['horizontal axis line','vertical axis line'],['X','Y']),
]
random.seed(20261022)
items=[]
for t in ('dark','light'):
    for n,r,b,m,tr in C: items.append((t,n,r.format(t=t),b,m,tr))
random.shuffle(items)
for d in ('in','sheets'): shutil.rmtree(f'{W}/{d}',ignore_errors=True); os.makedirs(f'{W}/{d}')
key={}; man={}
for i,(t,n,r,b,m,tr) in enumerate(items,1):
    cid=f'b{i:02d}'
    x,y,w,h=b
    src=f'{R}/{r}.png'; out=f'{W}/in/{cid}.png'
    subprocess.run(['magick',src,'-crop',f'{w}x{h}+{x}+{y}','+repage',out],check=True)
    key[cid]={'theme':t,'name':n,'raw':r,'box':b,'truth':tr}
    man[cid]=m
    sd=f'{W}/sims/{cid}'; os.makedirs(sd,exist_ok=True)
    subprocess.run(['python3',CVD,out,sd,'all'],check=True,capture_output=True)
    ims=[f'{sd}/{cid}-{k}.png' for k in ('normal','protan','deutan','tritan')]
    subprocess.run(['magick']+ims+['-filter','point','-resize','200%','-background','gray50','-splice','0x4','-append',f'{W}/sheets/{cid}.png'],check=True)
json.dump(key,open(W+'/key.json','w'),indent=1)
json.dump(man,open(W+'/manifest.json','w'),indent=1)
print(len(items))
