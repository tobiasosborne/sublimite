from PIL import Image, ImageDraw, ImageFont
D='/home/tobias/Projects/sublimite/docs/design/workups-2026-10-21/'
tiles=[('today (baseline.png, installed build, Xvfb 243 DPI)','baseline.png'),
('01 quiet tokens — dark','01-quiet-tokens/dark.png'),('01 quiet tokens — light','01-quiet-tokens/light.png'),
('02 calm chrome — dark','02-calm-chrome/dark.png'),('02 calm chrome — light','02-calm-chrome/light.png'),
('03 type and air — dark (Iosevka, 1.45)','03-type-and-air/dark.png'),
('04 rail — dark','04-rail/dark.png'),('05 floating — dark (palette open)','05-floating/dark.png'),None]
tw,th,lab,gap=960,600,64,24
S=Image.new('RGB',(3*tw+4*gap,3*(th+lab)+4*gap),(28,29,32)); dr=ImageDraw.Draw(S)
f=ImageFont.truetype('/usr/share/fonts/opentype/inter/Inter-Medium.otf',30)
for i,t in enumerate(tiles):
    x=gap+(i%3)*(tw+gap); y=gap+(i//3)*(th+lab+gap)
    if t is None:
        dr.text((x+10,y+20),'Ordered conservative (01) to experimental (05).',fill=(200,202,206),font=f)
        dr.text((x+10,y+70),'Each tile is the 2880x1800 PNG at one third.',fill=(156,158,164),font=f)
        dr.text((x+10,y+120),'See README.md for the costs.',fill=(156,158,164),font=f); continue
    im=Image.open(D+t[1]).convert('RGB').resize((tw,th),Image.LANCZOS)
    dr.text((x,y+12),t[0],fill=(220,222,226),font=f); S.paste(im,(x,y+lab))
S.save(D+'contact-sheet.png'); print(S.size)
