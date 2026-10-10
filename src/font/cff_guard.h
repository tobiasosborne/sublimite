/* Type 2 preflight, adapted from vendor/stb_truetype.h (same license).
 * Kept in the font boundary so no unchecked float conversion occurs before
 * admission. The vendor interpreter runs only after identical preflight.
 * Coordinates must also fit stb's signed 16-bit output vertices. */
static void font_cff_coord(float x, float y)
{
    if (!isfinite(x) || !isfinite(y) || x < -32768.0f || x > 32767.0f ||
        y < -32768.0f || y > 32767.0f) font_stb_assert_fail();
}
static void font_cff_move(stbtt__csctx *ctx, float dx, float dy)
{
    font_cff_coord(ctx->x + dx, ctx->y + dy);
    stbtt__csctx_rmove_to(ctx, dx, dy);
}
static void font_cff_line(stbtt__csctx *ctx, float dx, float dy)
{
    font_cff_coord(ctx->x + dx, ctx->y + dy);
    stbtt__csctx_rline_to(ctx, dx, dy);
}
static void font_cff_curve(stbtt__csctx *ctx, float dx1, float dy1,
                           float dx2, float dy2, float dx3, float dy3)
{
    float cx = ctx->x + dx1, cy = ctx->y + dy1;
    font_cff_coord(cx, cy);
    cx += dx2; cy += dy2; font_cff_coord(cx, cy);
    font_cff_coord(cx + dx3, cy + dy3);
    stbtt__csctx_rccurve_to(ctx, dx1, dy1, dx2, dy2, dx3, dy3);
}

/* UI CID selection must not linearly scan an arbitrarily large FDSelect or
 * parse a giant per-face/Private DICT outside the interpreter step budget. */
static int font_cff_ui_subrs(const stbtt_fontinfo *info, int glyph, stbtt__buf *subrs)
{
    stbtt__buf selection = info->fdselect;
    int format = stbtt__buf_get8(&selection), selector = -1;
    if (format == 0) {
        stbtt__buf_seek(&selection, 1 + glyph);
        selector = stbtt__buf_get8(&selection);
    } else if (format == 3) {
        int count = (int)stbtt__buf_get16(&selection), lo = 0, hi = count;
        while (lo < hi) {
            int mid = lo + (hi - lo) / 2;
            stbtt__buf_seek(&selection, 3 + 3 * mid);
            if ((int)stbtt__buf_get16(&selection) <= glyph) lo = mid + 1;
            else hi = mid;
        }
        if (lo) {
            stbtt__buf_seek(&selection, 3 + 3 * (lo - 1) + 2);
            selector = stbtt__buf_get8(&selection);
        }
    }
    if (selector < 0) return 0;
    stbtt__buf dict = stbtt__cff_index_get(info->fontdicts, selector);
    if (dict.size > 256) return 0;
    stbtt_uint32 private_extent[2] = {0, 0};
    stbtt__dict_get_ints(&dict, 18, 2, private_extent);
    if (private_extent[0] > 256u) return 0;
    *subrs = stbtt__get_subrs(info->cff, dict);
    return 1;
}

static int font_cff_run(const stbtt_fontinfo *info, int glyph_index, stbtt__csctx *c, int max_steps, int max_vertices)
{
   int in_header = 1, maskbits = 0, subr_stack_height = 0, sp = 0, v, i, b0;
   int steps = 0; // EDIT PATCH P2.3e
   int has_subrs = 0, clear_stack;
   float s[48];
   stbtt__buf subr_stack[10], subrs = info->subrs, b;
   float f;

#define STBTT__CSERR(s) (0)

   // this currently ignores the initial width value, which isn't needed if we have hmtx
   b = stbtt__cff_index_get(info->charstrings, glyph_index);
   while (b.cursor < b.size) {
      if (++steps > max_steps) return STBTT__CSERR("step budget"); // EDIT PATCH P2.3e
      i = 0;
      clear_stack = 1;
      b0 = stbtt__buf_get8(&b);
      switch (b0) {
      // @TODO implement hinting
      case 0x13: // hintmask
      case 0x14: // cntrmask
         if (in_header)
            maskbits += (sp / 2); // implicit "vstem"
         in_header = 0;
         stbtt__buf_skip(&b, (maskbits + 7) / 8);
         break;

      case 0x01: // hstem
      case 0x03: // vstem
      case 0x12: // hstemhm
      case 0x17: // vstemhm
         maskbits += (sp / 2);
         break;

      case 0x15: // rmoveto
         in_header = 0;
         if (sp < 2) return STBTT__CSERR("rmoveto stack");
         font_cff_move(c, s[sp-2], s[sp-1]);
         break;
      case 0x04: // vmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("vmoveto stack");
         font_cff_move(c, 0, s[sp-1]);
         break;
      case 0x16: // hmoveto
         in_header = 0;
         if (sp < 1) return STBTT__CSERR("hmoveto stack");
         font_cff_move(c, s[sp-1], 0);
         break;

      case 0x05: // rlineto
         if (sp < 2) return STBTT__CSERR("rlineto stack");
         for (; i + 1 < sp; i += 2)
            font_cff_line(c, s[i], s[i+1]);
         break;

      // hlineto/vlineto and vhcurveto/hvcurveto alternate horizontal and vertical
      // starting from a different place.

      case 0x07: // vlineto
         if (sp < 1) return STBTT__CSERR("vlineto stack");
         goto vlineto;
      case 0x06: // hlineto
         if (sp < 1) return STBTT__CSERR("hlineto stack");
         for (;;) {
            if (i >= sp) break;
            font_cff_line(c, s[i], 0);
            i++;
      vlineto:
            if (i >= sp) break;
            font_cff_line(c, 0, s[i]);
            i++;
         }
         break;

      case 0x1F: // hvcurveto
         if (sp < 4) return STBTT__CSERR("hvcurveto stack");
         goto hvcurveto;
      case 0x1E: // vhcurveto
         if (sp < 4) return STBTT__CSERR("vhcurveto stack");
         for (;;) {
            if (i + 3 >= sp) break;
            font_cff_curve(c, 0, s[i], s[i+1], s[i+2], s[i+3], (sp - i == 5) ? s[i + 4] : 0.0f);
            i += 4;
      hvcurveto:
            if (i + 3 >= sp) break;
            font_cff_curve(c, s[i], 0, s[i+1], s[i+2], (sp - i == 5) ? s[i+4] : 0.0f, s[i+3]);
            i += 4;
         }
         break;

      case 0x08: // rrcurveto
         if (sp < 6) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp; i += 6)
            font_cff_curve(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x18: // rcurveline
         if (sp < 8) return STBTT__CSERR("rcurveline stack");
         for (; i + 5 < sp - 2; i += 6)
            font_cff_curve(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         if (i + 1 >= sp) return STBTT__CSERR("rcurveline stack");
         font_cff_line(c, s[i], s[i+1]);
         break;

      case 0x19: // rlinecurve
         if (sp < 8) return STBTT__CSERR("rlinecurve stack");
         for (; i + 1 < sp - 6; i += 2)
            font_cff_line(c, s[i], s[i+1]);
         if (i + 5 >= sp) return STBTT__CSERR("rlinecurve stack");
         font_cff_curve(c, s[i], s[i+1], s[i+2], s[i+3], s[i+4], s[i+5]);
         break;

      case 0x1A: // vvcurveto
      case 0x1B: // hhcurveto
         if (sp < 4) return STBTT__CSERR("(vv|hh)curveto stack");
         f = 0.0;
         if (sp & 1) { f = s[i]; i++; }
         for (; i + 3 < sp; i += 4) {
            if (b0 == 0x1B)
               font_cff_curve(c, s[i], f, s[i+1], s[i+2], s[i+3], 0.0);
            else
               font_cff_curve(c, f, s[i], s[i+1], s[i+2], 0.0, s[i+3]);
            f = 0.0;
         }
         break;

      case 0x0A: // callsubr
         if (!has_subrs) {
            if (info->fdselect.size) {
               if (max_steps <= 2048) {
                  if (!font_cff_ui_subrs(info, glyph_index, &subrs))
                     return STBTT__CSERR("CID dictionary budget");
               } else subrs = stbtt__cid_get_glyph_subrs(info, glyph_index);
            }
            has_subrs = 1;
         }
         // FALLTHROUGH
      case 0x1D: // callgsubr
         if (sp < 1) return STBTT__CSERR("call(g|)subr stack");
         v = (int) s[--sp];
         if (subr_stack_height >= 10) return STBTT__CSERR("recursion limit");
         subr_stack[subr_stack_height++] = b;
         b = stbtt__get_subr(b0 == 0x0A ? subrs : info->gsubrs, v);
         if (b.size == 0) return STBTT__CSERR("subr not found");
         b.cursor = 0;
         clear_stack = 0;
         break;

      case 0x0B: // return
         if (subr_stack_height <= 0) return STBTT__CSERR("return outside subr");
         b = subr_stack[--subr_stack_height];
         clear_stack = 0;
         break;

      case 0x0E: // endchar
         stbtt__csctx_close_shape(c);
         return c->num_vertices <= max_vertices;

      case 0x0C: { // two-byte escape
         float dx1, dx2, dx3, dx4, dx5, dx6, dy1, dy2, dy3, dy4, dy5, dy6;
         float dx, dy;
         int b1 = stbtt__buf_get8(&b);
         switch (b1) {
         // @TODO These "flex" implementations ignore the flex-depth and resolution,
         // and always draw beziers.
         case 0x22: // hflex
            if (sp < 7) return STBTT__CSERR("hflex stack");
            dx1 = s[0];
            dx2 = s[1];
            dy2 = s[2];
            dx3 = s[3];
            dx4 = s[4];
            dx5 = s[5];
            dx6 = s[6];
            font_cff_curve(c, dx1, 0, dx2, dy2, dx3, 0);
            font_cff_curve(c, dx4, 0, dx5, -dy2, dx6, 0);
            break;

         case 0x23: // flex
            if (sp < 13) return STBTT__CSERR("flex stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = s[10];
            dy6 = s[11];
            //fd is s[12]
            font_cff_curve(c, dx1, dy1, dx2, dy2, dx3, dy3);
            font_cff_curve(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         case 0x24: // hflex1
            if (sp < 9) return STBTT__CSERR("hflex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dx4 = s[5];
            dx5 = s[6];
            dy5 = s[7];
            dx6 = s[8];
            font_cff_curve(c, dx1, dy1, dx2, dy2, dx3, 0);
            font_cff_curve(c, dx4, 0, dx5, dy5, dx6, -(dy1+dy2+dy5));
            break;

         case 0x25: // flex1
            if (sp < 11) return STBTT__CSERR("flex1 stack");
            dx1 = s[0];
            dy1 = s[1];
            dx2 = s[2];
            dy2 = s[3];
            dx3 = s[4];
            dy3 = s[5];
            dx4 = s[6];
            dy4 = s[7];
            dx5 = s[8];
            dy5 = s[9];
            dx6 = dy6 = s[10];
            dx = dx1+dx2+dx3+dx4+dx5;
            dy = dy1+dy2+dy3+dy4+dy5;
            if (STBTT_fabs(dx) > STBTT_fabs(dy))
               dy6 = -dy;
            else
               dx6 = -dx;
            font_cff_curve(c, dx1, dy1, dx2, dy2, dx3, dy3);
            font_cff_curve(c, dx4, dy4, dx5, dy5, dx6, dy6);
            break;

         default:
            return STBTT__CSERR("unimplemented");
         }
      } break;

      default:
         if (b0 != 255 && b0 != 28 && b0 < 32)
            return STBTT__CSERR("reserved operator");

         // push immediate
         if (b0 == 255) {
            f = (float)(stbtt_int32)stbtt__buf_get32(&b) / 0x10000;
         } else {
            stbtt__buf_skip(&b, -1);
            f = (float)(stbtt_int16)stbtt__cff_int(&b);
         }
         if (sp >= 48) return STBTT__CSERR("push stack overflow");
         s[sp++] = f;
         clear_stack = 0;
         break;
      }
      if (c->num_vertices > max_vertices) return STBTT__CSERR("vertex budget");
      if (clear_stack) sp = 0;
   }
   return STBTT__CSERR("no endchar");

#undef STBTT__CSERR
}

