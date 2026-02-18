#pragma once
// =============================================================================
// UIHtmlCss.h – HTML+CSS ↔ UIElement serialization
// =============================================================================

#include "UIElement.h"

#include <cstdio>
#include <map>
#include <regex>
#include <sstream>
#include <string>

namespace myu::ui {

// ─── Export to HTML+CSS ─────────────────────────────────────────────────────

inline std::string colorToCSS(const Vec4& c) {
    if (c.w >= 1.0f) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#%02x%02x%02x",
            static_cast<int>(c.x * 255), static_cast<int>(c.y * 255),
            static_cast<int>(c.z * 255));
        return buf;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%.2f)",
        static_cast<int>(c.x * 255), static_cast<int>(c.y * 255),
        static_cast<int>(c.z * 255), c.w);
    return buf;
}

inline bool parseColor(const std::string& s, Vec4& out) {
    // #rrggbb
    if (s.size() == 7 && s[0] == '#') {
        unsigned int r, g, b;
        if (std::sscanf(s.c_str(), "#%02x%02x%02x", &r, &g, &b) == 3) {
            out = {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
            return true;
        }
    }
    // rgba(r,g,b,a)
    float r, g, b, a;
    if (std::sscanf(s.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) == 4) {
        out = {r / 255.0f, g / 255.0f, b / 255.0f, a};
        return true;
    }
    // rgb(r,g,b)
    if (std::sscanf(s.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3) {
        out = {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
        return true;
    }
    return false;
}

inline const char* elementTag(ElementType t) {
    switch (t) {
        case ElementType::Button:    return "button";
        case ElementType::Label:     return "span";
        case ElementType::Image:     return "img";
        case ElementType::Input:     return "input";
        case ElementType::Checkbox:  return "div";
        case ElementType::Toggle:    return "div";
        case ElementType::Slider:    return "div";
        case ElementType::Progress:  return "div";
        case ElementType::Dropdown:  return "div";
        case ElementType::Container: return "div";
        default:                     return "div";
    }
}

inline std::string anchorToCSS(const Anchor& a, const Style& style) {
    std::ostringstream css;
    css << "position: absolute; ";

    bool stretchX = (a.min.x != a.max.x);
    bool stretchY = (a.min.y != a.max.y);

    if (stretchX) {
        css << "left: calc(" << (a.min.x * 100) << "% + " << a.offsetMin.x << "px); ";
        css << "right: calc(" << ((1 - a.max.x) * 100) << "% + " << (-a.offsetMax.x) << "px); ";
    } else {
        float anchorPct = a.min.x * 100;
        float offset = a.offsetMin.x;
        float w = a.offsetMax.x - a.offsetMin.x;
        css << "left: calc(" << anchorPct << "% + " << offset << "px); ";
        css << "width: " << w << "px; ";
    }

    if (stretchY) {
        css << "top: calc(" << (a.min.y * 100) << "% + " << a.offsetMin.y << "px); ";
        css << "bottom: calc(" << ((1 - a.max.y) * 100) << "% + " << (-a.offsetMax.y) << "px); ";
    } else {
        float anchorPct = a.min.y * 100;
        float offset = a.offsetMin.y;
        float h = a.offsetMax.y - a.offsetMin.y;
        css << "top: calc(" << anchorPct << "% + " << offset << "px); ";
        css << "height: " << h << "px; ";
    }

    return css.str();
}

inline std::string elementToCSS(const UIElement& e) {
    std::ostringstream css;
    css << anchorToCSS(e.anchor, e.style);

    auto& s = e.style;
    css << "background: " << colorToCSS(s.bgColor)     << "; ";
    css << "color: "      << colorToCSS(s.fgColor)      << "; ";
    if (s.borderWidth > 0)
        css << "border: " << s.borderWidth << "px solid " << colorToCSS(s.borderColor) << "; ";
    else
        css << "border: none; ";
    if (s.borderRadius > 0)
        css << "border-radius: " << s.borderRadius << "px; ";
    css << "font-size: " << s.fontSize << "px; ";
    if (s.opacity < 1.0f)
        css << "opacity: " << s.opacity << "; ";
    css << "padding: " << s.padding[0] << "px " << s.padding[1] << "px "
        << s.padding[2] << "px " << s.padding[3] << "px; ";
    css << "box-sizing: border-box; ";
    if (!e.visible)
        css << "display: none; ";

    return css.str();
}

inline void exportElementHTML(const UIElement& e, std::ostringstream& out, int indent) {
    std::string pad(indent * 2, ' ');
    const char* tag = elementTag(e.type);

    std::string classAttr;
    if (!e.cssClass.empty())
        classAttr = " class=\"" + e.cssClass + "\"";

    std::string idAttr = " id=\"" + e.name + "\"";
    std::string typeAttr = std::string(" data-myu-type=\"") + elementTypeName(e.type) + "\"";
    std::string styleAttr = " style=\"" + elementToCSS(e) + "\"";

    if (e.type == ElementType::Image) {
        out << pad << "<img" << idAttr << classAttr << typeAttr << styleAttr;
        if (!e.imagePath.empty()) out << " src=\"" << e.imagePath << "\"";
        out << " />\n";
        return;
    }
    if (e.type == ElementType::Input) {
        out << pad << "<input" << idAttr << classAttr << typeAttr << styleAttr;
        if (!e.text.empty()) out << " placeholder=\"" << e.text << "\"";
        out << " />\n";
        return;
    }

    out << pad << "<" << tag << idAttr << classAttr << typeAttr << styleAttr << ">\n";

    if (!e.text.empty() && e.children.empty())
        out << pad << "  " << e.text << "\n";

    for (auto& child : e.children)
        exportElementHTML(*child, out, indent + 1);

    out << pad << "</" << tag << ">\n";
}

inline std::string exportToHTML(const UIElement& root, int canvasW = 800, int canvasH = 600) {
    std::ostringstream out;
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    out << "  <meta charset=\"UTF-8\">\n";
    out << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    out << "  <title>MyuEngine UI Export</title>\n";
    out << "  <style>\n";
    out << "    * { margin: 0; padding: 0; box-sizing: border-box; }\n";
    out << "    body { background: #1a1a2e; overflow: hidden; }\n";
    out << "    #myu-root {\n";
    out << "      position: relative;\n";
    out << "      width: " << canvasW << "px;\n";
    out << "      height: " << canvasH << "px;\n";
    out << "      margin: 0 auto;\n";
    out << "      background: #16213e;\n";
    out << "    }\n";
    out << "  </style>\n";
    out << "</head>\n<body>\n";
    out << "<div id=\"myu-root\">\n";

    for (auto& child : root.children)
        exportElementHTML(*child, out, 1);

    out << "</div>\n</body>\n</html>\n";
    return out.str();
}

// ─── Import from HTML (simplified parser) ───────────────────────────────────

// Minimal parser: reads back HTML exported by exportToHTML.
// Handles inline style attributes to reconstruct UIElement tree.

struct CSSProps {
    std::map<std::string, std::string> props;

    void parse(const std::string& styleStr) {
        std::istringstream ss(styleStr);
        std::string token;
        while (std::getline(ss, token, ';')) {
            auto colon = token.find(':');
            if (colon == std::string::npos) continue;
            std::string key = token.substr(0, colon);
            std::string val = token.substr(colon + 1);
            // trim
            auto trim = [](std::string& s) {
                size_t start = s.find_first_not_of(" \t\n\r");
                size_t end   = s.find_last_not_of(" \t\n\r");
                s = (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
            };
            trim(key); trim(val);
            props[key] = val;
        }
    }

    std::string get(const std::string& key, const std::string& def = "") const {
        auto it = props.find(key);
        return it != props.end() ? it->second : def;
    }

    float getFloat(const std::string& key, float def = 0) const {
        auto it = props.find(key);
        if (it == props.end()) return def;
        return std::stof(it->second);
    }
};

inline float parseCalcPctPx(const std::string& val, float& pct) {
    // "calc(50% + 10px)" → pct=50, px=10
    pct = 0; float px = 0;
    std::sscanf(val.c_str(), "calc(%f%% + %fpx)", &pct, &px);
    // also try negative: "calc(50% + -10px)"
    return px;
}

inline float parsePx(const std::string& val) {
    float v = 0;
    std::sscanf(val.c_str(), "%fpx", &v);
    return v;
}

inline void cssToAnchor(const CSSProps& css, Anchor& a) {
    std::string left   = css.get("left");
    std::string right  = css.get("right");
    std::string top    = css.get("top");
    std::string bottom = css.get("bottom");
    std::string width  = css.get("width");
    std::string height = css.get("height");

    // X axis
    if (!left.empty() && !width.empty()) {
        float pct, px;
        px = parseCalcPctPx(left, pct);
        float w = parsePx(width);
        a.min.x = a.max.x = pct / 100.0f;
        a.offsetMin.x = px;
        a.offsetMax.x = px + w;
    } else if (!left.empty() && !right.empty()) {
        float lpct, lpx, rpct, rpx;
        lpx = parseCalcPctPx(left, lpct);
        rpx = parseCalcPctPx(right, rpct);
        a.min.x = lpct / 100.0f;
        a.max.x = 1.0f - rpct / 100.0f;
        a.offsetMin.x = lpx;
        a.offsetMax.x = -rpx;
    }

    // Y axis
    if (!top.empty() && !height.empty()) {
        float pct, px;
        px = parseCalcPctPx(top, pct);
        float h = parsePx(height);
        a.min.y = a.max.y = pct / 100.0f;
        a.offsetMin.y = px;
        a.offsetMax.y = px + h;
    } else if (!top.empty() && !bottom.empty()) {
        float tpct, tpx, bpct, bpx;
        tpx = parseCalcPctPx(top, tpct);
        bpx = parseCalcPctPx(bottom, bpct);
        a.min.y = tpct / 100.0f;
        a.max.y = 1.0f - bpct / 100.0f;
        a.offsetMin.y = tpx;
        a.offsetMax.y = -bpx;
    }
}

inline void cssToStyle(const CSSProps& css, Style& s) {
    std::string bg = css.get("background");
    if (!bg.empty()) parseColor(bg, s.bgColor);

    std::string fg = css.get("color");
    if (!fg.empty()) parseColor(fg, s.fgColor);

    std::string border = css.get("border");
    if (!border.empty() && border != "none") {
        float bw = 0;
        char color[64] = {};
        if (std::sscanf(border.c_str(), "%fpx solid %63s", &bw, color) >= 1) {
            s.borderWidth = bw;
            parseColor(color, s.borderColor);
        }
    }

    std::string br = css.get("border-radius");
    if (!br.empty()) s.borderRadius = parsePx(br);

    std::string fs = css.get("font-size");
    if (!fs.empty()) s.fontSize = parsePx(fs);

    std::string op = css.get("opacity");
    if (!op.empty()) s.opacity = std::stof(op);

    std::string pad = css.get("padding");
    if (!pad.empty()) {
        float t, r, b, l;
        if (std::sscanf(pad.c_str(), "%fpx %fpx %fpx %fpx", &t, &r, &b, &l) == 4) {
            s.padding[0] = t; s.padding[1] = r;
            s.padding[2] = b; s.padding[3] = l;
        }
    }
}

inline ElementType nameToType(const std::string& name) {
    for (int i = 0; i < static_cast<int>(ElementType::kCount); ++i) {
        auto t = static_cast<ElementType>(i);
        if (name == elementTypeName(t))
            return t;
    }
    return ElementType::Panel;
}

inline ElementType tagToType(const std::string& tag) {
    if (tag == "button") return ElementType::Button;
    if (tag == "span")   return ElementType::Label;
    if (tag == "img")    return ElementType::Image;
    if (tag == "input")  return ElementType::Input;
    if (tag == "select") return ElementType::Dropdown;
    if (tag == "progress") return ElementType::Progress;
    return ElementType::Panel;
}

// Simplified HTML parser — handles our own export format
inline void importFromHTML(const std::string& html, UIElement& root) {
    root.children.clear();

    // Find content inside <div id="myu-root">...</div>
    auto rootStart = html.find("id=\"myu-root\"");
    if (rootStart == std::string::npos) return;
    rootStart = html.find('>', rootStart);
    if (rootStart == std::string::npos) return;
    ++rootStart;

    // Simple tag-based parser using regex
    // Match: <tag id="..." class="..." style="..."> or <tag ... />
    std::regex tagRx(R"(<(\w+)\s+([^>]*?)(/?)>)", std::regex::optimize);
    std::regex closeRx(R"(</(\w+)>)", std::regex::optimize);
    std::regex attrRx(R"re(([\w-]+)="([^"]*)")re");
    std::string body = html.substr(rootStart);
    std::vector<UIElement*> stack;
    stack.push_back(&root);

    auto pos = body.cbegin();
    while (pos != body.cend()) {
        // Try closing tag
        std::smatch cm;
        if (std::regex_search(pos, body.cend(), cm, closeRx) && cm.position() == 0) {
            if (stack.size() > 1) stack.pop_back();
            pos = cm.suffix().first;
            continue;
        }

        // Try opening tag
        std::smatch om;
        if (std::regex_search(pos, body.cend(), om, tagRx) && om.position() == 0) {
            std::string tag      = om[1].str();
            std::string attrStr  = om[2].str();
            bool selfClose       = (om[3].str() == "/");

            if (tag == "div" || tag == "button" || tag == "span" ||
                tag == "img" || tag == "input" || tag == "select" ||
                tag == "progress")
            {
                // Parse attributes
                std::map<std::string, std::string> attrs;
                auto aBegin = std::sregex_iterator(attrStr.begin(), attrStr.end(), attrRx);
                auto aEnd   = std::sregex_iterator();
                for (auto it = aBegin; it != aEnd; ++it) {
                    attrs[(*it)[1].str()] = (*it)[2].str();
                }

                ElementType mapped = tagToType(tag);
                if (attrs.count("data-myu-type"))
                    mapped = nameToType(attrs["data-myu-type"]);
                auto elem = createElement(mapped);
                if (attrs.count("id")) elem->name = attrs["id"];
                if (attrs.count("class")) elem->cssClass = attrs["class"];

                if (attrs.count("style")) {
                    CSSProps css;
                    css.parse(attrs["style"]);
                    cssToAnchor(css, elem->anchor);
                    cssToStyle(css, elem->style);
                }

                UIElement* raw = elem.get();
                stack.back()->addChild(std::move(elem));

                if (!selfClose)
                    stack.push_back(raw);
            }
            pos = om.suffix().first;
            continue;
        }

        // Text content — assign to current parent if it's a leaf type
        auto nextTag = std::find(pos, body.cend(), '<');
        if (nextTag != pos) {
            std::string text(pos, nextTag);
            // trim
            size_t s = text.find_first_not_of(" \t\n\r");
            size_t e = text.find_last_not_of(" \t\n\r");
            if (s != std::string::npos) {
                text = text.substr(s, e - s + 1);
                if (!text.empty() && !stack.empty()) {
                    auto* cur = stack.back();
                    if (cur->type == ElementType::Label ||
                        cur->type == ElementType::Button ||
                        cur->type == ElementType::Checkbox ||
                        cur->type == ElementType::Toggle ||
                        cur->type == ElementType::Slider ||
                        cur->type == ElementType::Progress ||
                        cur->type == ElementType::Dropdown)
                        cur->text = text;
                }
            }
        }
        pos = nextTag;
    }
}

// ─── JSON-like project save/load (simple key=value for .myu-ui) ─────────

inline void exportElementJSON(const UIElement& e, std::ostringstream& out, int indent) {
    std::string pad(indent * 2, ' ');
    out << pad << "{\n";
    out << pad << "  \"id\": " << e.id << ",\n";
    out << pad << "  \"name\": \"" << e.name << "\",\n";
    out << pad << "  \"type\": \"" << elementTypeName(e.type) << "\",\n";
    out << pad << "  \"visible\": " << (e.visible ? "true" : "false") << ",\n";
    out << pad << "  \"text\": \"" << e.text << "\",\n";
    out << pad << "  \"cssClass\": \"" << e.cssClass << "\",\n";
    out << pad << "  \"anchor\": {\n";
    out << pad << "    \"min\": [" << e.anchor.min.x << "," << e.anchor.min.y << "],\n";
    out << pad << "    \"max\": [" << e.anchor.max.x << "," << e.anchor.max.y << "],\n";
    out << pad << "    \"offsetMin\": [" << e.anchor.offsetMin.x << "," << e.anchor.offsetMin.y << "],\n";
    out << pad << "    \"offsetMax\": [" << e.anchor.offsetMax.x << "," << e.anchor.offsetMax.y << "],\n";
    out << pad << "    \"pivot\": [" << e.anchor.pivot.x << "," << e.anchor.pivot.y << "]\n";
    out << pad << "  },\n";
    out << pad << "  \"style\": {\n";
    out << pad << "    \"bgColor\": \"" << colorToCSS(e.style.bgColor) << "\",\n";
    out << pad << "    \"fgColor\": \"" << colorToCSS(e.style.fgColor) << "\",\n";
    out << pad << "    \"borderColor\": \"" << colorToCSS(e.style.borderColor) << "\",\n";
    out << pad << "    \"borderWidth\": " << e.style.borderWidth << ",\n";
    out << pad << "    \"borderRadius\": " << e.style.borderRadius << ",\n";
    out << pad << "    \"fontSize\": " << e.style.fontSize << ",\n";
    out << pad << "    \"opacity\": " << e.style.opacity << "\n";
    out << pad << "  },\n";
    out << pad << "  \"overrides\": [\n";
    for (size_t i = 0; i < e.overrides.size(); ++i) {
        const auto& o = e.overrides[i];
        out << pad << "    {\n";
        out << pad << "      \"name\": \"" << o.name << "\",\n";
        out << pad << "      \"useAnchor\": " << (o.useAnchor ? "true" : "false") << ",\n";
        out << pad << "      \"useStyle\": " << (o.useStyle ? "true" : "false") << ",\n";
        out << pad << "      \"anchor\": {\n";
        out << pad << "        \"min\": [" << o.anchor.min.x << "," << o.anchor.min.y << "],\n";
        out << pad << "        \"max\": [" << o.anchor.max.x << "," << o.anchor.max.y << "],\n";
        out << pad << "        \"offsetMin\": [" << o.anchor.offsetMin.x << "," << o.anchor.offsetMin.y << "],\n";
        out << pad << "        \"offsetMax\": [" << o.anchor.offsetMax.x << "," << o.anchor.offsetMax.y << "],\n";
        out << pad << "        \"pivot\": [" << o.anchor.pivot.x << "," << o.anchor.pivot.y << "]\n";
        out << pad << "      },\n";
        out << pad << "      \"style\": {\n";
        out << pad << "        \"bgColor\": \"" << colorToCSS(o.style.bgColor) << "\",\n";
        out << pad << "        \"fgColor\": \"" << colorToCSS(o.style.fgColor) << "\",\n";
        out << pad << "        \"borderColor\": \"" << colorToCSS(o.style.borderColor) << "\",\n";
        out << pad << "        \"borderWidth\": " << o.style.borderWidth << ",\n";
        out << pad << "        \"borderRadius\": " << o.style.borderRadius << ",\n";
        out << pad << "        \"fontSize\": " << o.style.fontSize << ",\n";
        out << pad << "        \"opacity\": " << o.style.opacity << "\n";
        out << pad << "      }\n";
        out << pad << "    }";
        if (i + 1 < e.overrides.size()) out << ",";
        out << "\n";
    }
    out << pad << "  ],\n";
    out << pad << "  \"children\": [\n";
    for (size_t i = 0; i < e.children.size(); ++i) {
        exportElementJSON(*e.children[i], out, indent + 2);
        if (i + 1 < e.children.size()) out << ",";
        out << "\n";
    }
    out << pad << "  ]\n";
    out << pad << "}";
}

inline std::string exportToJSON(const UIElement& root) {
    std::ostringstream out;
    exportElementJSON(root, out, 0);
    out << "\n";
    return out.str();
}

} // namespace myu::ui
