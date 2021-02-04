/*
 * Copyright (c) 2019 Anton Stiopin astiopin@gmail.com
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "sdf_atlas.h"

#include <algorithm>
#include <unordered_set>
#include <iostream>
#include <sstream>

void SdfAtlas::init( Font *font, float tex_width, float row_height, float sdf_size ) {
    this->font = font;

    glyph_rects.clear();

    this->tex_width  = tex_width;
    this->row_height = row_height;
    this->sdf_size   = sdf_size;
    glyph_count = 0;
    posx = 0.0f;
    posy = 0.0f;
    max_height = row_height + sdf_size * 2.0f;
}

void SdfAtlas::allocate_codepoint( uint32_t codepoint ) {
    int glyph_idx = font->glyph_idx( codepoint );
    if ( glyph_idx == -1 ) return;
    if ( glyph_idx == 0 ) return;
    const Glyph& g = font->glyphs[ glyph_idx ];
    if ( g.command_count <= 2 ) return;
    
    float fheight = font->ascent - font->descent;
    float scale = row_height / fheight;
    float rect_width = ( g.max.x - g.min.x ) * scale + sdf_size * 2.0f;
    float rect_height = (g.max.y - g.min.y) * scale + sdf_size * 2.0f;
    float row_and_border = row_height + sdf_size * 2.0f;

    if ( ( posx + rect_width ) > tex_width ) {
        posx = 0.0f;
        
        posy = ceil( posy + row_and_border );
        max_height = ceil( posy + row_and_border );
    }

    /* Calculate the top of the glyph (including upper border) in terms of atlas image coordinates in pixels.
    Because the y-axis in the em-grid points up, we have to take g.max.y. */
    float rect_pos_y = posy + (g.min.y - font->descent) * scale;

    GlyphRect gr;
    gr.codepoint = codepoint;
    gr.glyph_idx = glyph_idx;
    gr.x0 = posx;
    gr.x1 = posx + rect_width;
    gr.y0 = rect_pos_y;
    gr.y1 = rect_pos_y + rect_height;

    glyph_rects.push_back( gr );

    posx = ceil( posx + rect_width );
    glyph_count++;
}

void SdfAtlas::allocate_all_glyphs() {
    for ( auto kv : font->glyph_map ) {
        allocate_codepoint( kv.first );
    }
}

void SdfAtlas::allocate_unicode_range( uint32_t start, uint32_t end ) {
    for ( uint32_t ucp = start; ucp <= end; ++ucp ) {
        allocate_codepoint( ucp );
    }
}

void SdfAtlas::draw_glyphs( GlyphPainter& gp ) const {
    float fheight = font->ascent - font->descent;
    float scale = row_height / fheight;
    float baseline = -font->descent * scale;
    
    for ( size_t iglyph = 0; iglyph < glyph_rects.size(); ++iglyph ) {
        const GlyphRect& gr = glyph_rects[ iglyph ];
        /* Take bearingX and bearingY into account. */
        float left = font->glyphs[ gr.glyph_idx ].left_side_bearing * scale;
        float top = (font->glyphs[gr.glyph_idx].min.y - font->descent) * scale;
        F2 glyph_pos = F2 { gr.x0, gr.y0 + baseline } + F2 { sdf_size - left, sdf_size - top };
        gp.draw_glyph( font, gr.glyph_idx, glyph_pos, scale, sdf_size );
    }
}

template<typename K, typename V>
std::unordered_map<V, K> inverse_map(std::unordered_map<K, V>& map)
{
    std::unordered_map<V, K> inv;
    std::for_each(map.begin(), map.end(),
        [&inv](const std::pair<K, V>& p)
        {
            inv.insert(std::make_pair(p.second, p.first));
        });
    return inv;
}

std::string SdfAtlas::json(float tex_height) const {
    float fheight = font->ascent - font->descent;
    float scaley = row_height / tex_height / fheight;
    float scalex = row_height / tex_width / fheight;

    const Glyph& gspace = font->glyphs[font->glyph_idx(' ')];
    const Glyph& gx = font->glyphs[font->glyph_idx('x')];
    const Glyph& gxcap = font->glyphs[font->glyph_idx('X')];

    std::unordered_set<uint32_t> codepoints;
    for (size_t igr = 0; igr < glyph_rects.size(); ++igr) {
        codepoints.insert(glyph_rects[igr].codepoint);
    }

    std::stringstream ss;
    ss << "/* The char metrics are stored in an object with the Unicode code point as the key and with values of the form:" << std::endl;
    ss << "[left, top, right, bottom, bearingX, bearingY, advanceX, flags]." << std::endl;
    ss << "The flags indicate the char type (Lower = 1, Upper = 2, Punct = 4, Space = 8)." << std::endl;
    ss << "The kerning pairs are stored in an object with the Unicode code point of the left character as the key and with values of the form:" << std::endl;
    ss << "{ rightCharCode1: kerningValue1, ..., rightCharCodeN: kerningValueN }. */" << std::endl;
    ss << "export default {" << std::endl;
    ss << "  textureWidth: " << tex_width << ", /* Width of the glyph atlas texture in pixel. */" << std::endl;
    ss << "  textureHeight: " << tex_height << ", /* Height of the glyph atlas texture in pixel. */" << std::endl;
    ss << "  falloff: " << sdf_size << ", /* SDF border on each side in pixel. */" << std::endl;
    ss << "  glyphHeight: " << row_height << ", /* Maximum height (without border, just ascent + abs(descent)) of an individual glyph texture in pixel. */" << std::endl;
    ss << "  /* Below this line, all metrics are normalized to the ascent (ascent = 1)." << std::endl;
    ss << "  Only the glyph bounding box [left, top, right, bottom] is given in absolute pixels where (0,0) is top left of the glyph atlas. */" << std::endl;
    ss << "  descent: " << font->descent / font->ascent << "," << std::endl;
    ss << "  lineGap: " << font->line_gap / font->ascent << "," << std::endl;
    ss << "  capHeight: " << gxcap.max.y / font->ascent << "," << std::endl;
    ss << "  xHeight: " << gx.max.y / font->ascent << "," << std::endl;
    ss << "  advanceXSpace: " << gspace.advance_width / font->ascent << "," << std::endl;
    ss << "  chars: {";
    for (size_t igr = 0; igr < glyph_rects.size(); ++igr) {
        const GlyphRect& gr = glyph_rects[igr];
        const Glyph& g = font->glyphs[gr.glyph_idx];
        float tcLeft = gr.x0;
        float tcTop = tex_height - gr.y1;
        float tcRight = gr.x1;
        float tcBottom = tex_height - gr.y0;

        if (igr > 0) {
            ss << ",";
        }
        ss << " " << gr.codepoint << ": [" << tcLeft << ", " << tcTop << ", " << tcRight << ", " << tcBottom << ", " << g.left_side_bearing / font->ascent << ", " << g.max.y / font->ascent << ", " << g.advance_width / font->ascent << ", " << (int)g.char_type << "]" ;
    }

    ss << " }," << std::endl;   

    /* Inverted glyph map: glyph index -> codepoint */
    std::unordered_map<int, uint32_t> invertedGlyphMap = inverse_map(font->glyph_map);

    /* Order the kernings by the first character (create a map from the unicode of the first char to all belonging kerning pairs with the second char and the kerning value). */
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, float>>  kernings_all;
    for (auto kv : font->kern_map) {
        uint32_t kern_pair = kv.first;
        float kern_value = kv.second;
        int kern_first_glyph_idx = (kern_pair >> 16) & 0xffff;
        int kern_second_glyph_idx = kern_pair & 0xffff;
        uint32_t kern_first_code_point = invertedGlyphMap.at(kern_first_glyph_idx);
        uint32_t kern_second_code_point = invertedGlyphMap.at(kern_second_glyph_idx);

        bool first_found = codepoints.find(kern_first_code_point) != codepoints.end();
        bool second_found = codepoints.find(kern_second_code_point) != codepoints.end();
        if (first_found && second_found) {
            auto it = kernings_all.find(kern_first_code_point);        
            if (it == kernings_all.end()) {
                /* No kerning pair has been added so far in which this char is the left one -> add a new map for this char. */
                std::unordered_map<uint32_t, float> new_kernings_single;
                new_kernings_single.insert({ kern_second_code_point , kern_value });
                kernings_all.insert({ kern_first_code_point, new_kernings_single });
            }
            else {
                it->second.insert({ kern_second_code_point , kern_value });
            }
            
        }
    }

    /* Create json output for the map created above. */
    ss << "  kerning: {";
    bool is_start_all = true;
    for (auto kv_all : kernings_all) {
        uint32_t kern_first_code_point = kv_all.first;
        std::unordered_map<uint32_t, float> kernings_single = kv_all.second;
        if (!is_start_all) { ss << ",";  }
        ss << " " << kern_first_code_point << ": {";

        bool is_start_single = true;
        for (auto kv_single : kernings_single) {
            if (!is_start_single) { ss << ","; }
            ss << " " << kv_single.first << ": " << kv_single.second / font->ascent;
            is_start_single = false;
        }

        ss << " }";
        is_start_all = false;
    }

    ss << " }" << std::endl;

    ss << "};" << std::endl;

    return ss.str();
}