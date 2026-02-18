#pragma once
// =============================================================================
// UIExporters.h – Component package exporters for UI Designer
// =============================================================================

#include "UIHtmlCss.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace myu::ui {

enum class ComponentTarget {
    Web,
    Xml,
    Desktop
};

struct ComponentBreakpoint {
    std::string name;
    int width = 0;
    int height = 0;
};

struct ComponentPackageInfo {
    std::string name;
    int canvasW = 800;
    int canvasH = 600;
    std::string version = "1.0";
    std::vector<ComponentBreakpoint> breakpoints;
};

inline const char* componentTargetName(ComponentTarget t) {
    switch (t) {
        case ComponentTarget::Web:     return "web";
        case ComponentTarget::Xml:     return "xml";
        case ComponentTarget::Desktop: return "desktop";
        default:                       return "unknown";
    }
}

inline std::string sanitizeName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out.push_back(c);
        } else if (c == ' ') {
            out.push_back('_');
        }
    }
    if (out.empty()) out = "Component";
    return out;
}

inline bool writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return true;
}

inline std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

inline std::string vec2ToAttr(const Vec2& v) {
    std::ostringstream ss;
    ss << v.x << "," << v.y;
    return ss.str();
}

inline std::string vec4ToAttr(const Vec4& v) {
    std::ostringstream ss;
    ss << v.x << "," << v.y << "," << v.z << "," << v.w;
    return ss.str();
}

inline void exportElementXML(const UIElement& e, std::ostringstream& out, int indent) {
    std::string pad(indent * 2, ' ');
    out << pad << "<Element";
    out << " type=\"" << elementTypeName(e.type) << "\"";
    out << " id=\"" << e.id << "\"";
    out << " name=\"" << xmlEscape(e.name) << "\"";
    out << " visible=\"" << (e.visible ? "1" : "0") << "\"";
    out << " locked=\"" << (e.locked ? "1" : "0") << "\"";
    out << " rotation=\"" << e.rotation << "\"";
    if (!e.text.empty()) out << " text=\"" << xmlEscape(e.text) << "\"";
    if (!e.imagePath.empty()) out << " imagePath=\"" << xmlEscape(e.imagePath) << "\"";
    if (!e.cssClass.empty()) out << " cssClass=\"" << xmlEscape(e.cssClass) << "\"";
    out << ">\n";

    out << pad << "  <Anchor";
    out << " min=\"" << vec2ToAttr(e.anchor.min) << "\"";
    out << " max=\"" << vec2ToAttr(e.anchor.max) << "\"";
    out << " offsetMin=\"" << vec2ToAttr(e.anchor.offsetMin) << "\"";
    out << " offsetMax=\"" << vec2ToAttr(e.anchor.offsetMax) << "\"";
    out << " pivot=\"" << vec2ToAttr(e.anchor.pivot) << "\"";
    out << " />\n";

    const Style& s = e.style;
    out << pad << "  <Style";
    out << " bgColor=\"" << vec4ToAttr(s.bgColor) << "\"";
    out << " fgColor=\"" << vec4ToAttr(s.fgColor) << "\"";
    out << " borderColor=\"" << vec4ToAttr(s.borderColor) << "\"";
    out << " borderWidth=\"" << s.borderWidth << "\"";
    out << " borderRadius=\"" << s.borderRadius << "\"";
    out << " fontSize=\"" << s.fontSize << "\"";
    out << " opacity=\"" << s.opacity << "\"";
    out << " padding=\"" << s.padding[0] << "," << s.padding[1] << "," << s.padding[2]
        << "," << s.padding[3] << "\"";
    out << " />\n";

    for (auto& child : e.children)
        exportElementXML(*child, out, indent + 1);

    out << pad << "</Element>\n";
}

inline std::string exportToXML(const UIElement& root, const ComponentPackageInfo& info) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<Component name=\"" << xmlEscape(info.name) << "\" version=\"" << info.version
        << "\" canvas=\"" << info.canvasW << "x" << info.canvasH << "\">\n";
    for (auto& child : root.children)
        exportElementXML(*child, out, 1);
    out << "</Component>\n";
    return out.str();
}

inline std::string buildManifest(const ComponentPackageInfo& info, ComponentTarget target) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"name\": \"" << info.name << "\",\n";
    out << "  \"target\": \"" << componentTargetName(target) << "\",\n";
    out << "  \"version\": \"" << info.version << "\",\n";
    out << "  \"canvas\": {\"w\": " << info.canvasW << ", \"h\": " << info.canvasH << "},\n";
    out << "  \"breakpoints\": [\n";
    for (size_t i = 0; i < info.breakpoints.size(); ++i) {
        const auto& bp = info.breakpoints[i];
        out << "    {\"name\": \"" << bp.name << "\", \"w\": " << bp.width
            << ", \"h\": " << bp.height << "}";
        if (i + 1 < info.breakpoints.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n";
    out << "}\n";
    return out.str();
}

inline std::string elementCssId(const UIElement& e) {
    return sanitizeName(e.name) + "_" + std::to_string(e.id);
}

inline std::string importantSuffix(bool important) {
    return important ? " !important" : "";
}

inline std::string buildElementCSS(const Anchor& a, const Style& s, bool visible, bool important) {
    std::ostringstream css;
    std::string imp = importantSuffix(important);
    std::string anchorCss = anchorToCSS(a, s);
    if (important) {
        std::ostringstream patched;
        std::istringstream ss(anchorCss);
        std::string token;
        while (std::getline(ss, token, ';')) {
            if (token.find_first_not_of(" \t\n\r") == std::string::npos) continue;
            patched << token << imp << "; ";
        }
        anchorCss = patched.str();
    }
    css << anchorCss;
    css << "background: " << colorToCSS(s.bgColor) << imp << "; ";
    css << "color: " << colorToCSS(s.fgColor) << imp << "; ";
    if (s.borderWidth > 0)
        css << "border: " << s.borderWidth << "px solid " << colorToCSS(s.borderColor) << imp << "; ";
    else
        css << "border: none" << imp << "; ";
    if (s.borderRadius > 0)
        css << "border-radius: " << s.borderRadius << "px" << imp << "; ";
    css << "font-size: " << s.fontSize << "px" << imp << "; ";
    if (s.opacity < 1.0f)
        css << "opacity: " << s.opacity << imp << "; ";
    css << "padding: " << s.padding[0] << "px " << s.padding[1] << "px "
        << s.padding[2] << "px " << s.padding[3] << "px" << imp << "; ";
    css << "box-sizing: border-box" << imp << "; ";
    if (!visible)
        css << "display: none" << imp << "; ";
    return css.str();
}

inline void exportElementWebHTML(const UIElement& e, std::ostringstream& out, int indent) {
    std::string pad(indent * 2, ' ');
    const char* tag = elementTag(e.type);
    std::string id = elementCssId(e);

    std::string classAttr;
    if (!e.cssClass.empty())
        classAttr = " class=\"" + e.cssClass + "\"";

    std::string idAttr = " id=\"" + id + "\"";
    std::string typeAttr = std::string(" data-myu-type=\"") + elementTypeName(e.type) + "\"";
    std::string nameAttr = std::string(" data-myu-name=\"") + e.name + "\"";

    if (e.type == ElementType::Image) {
        out << pad << "<img" << idAttr << classAttr << typeAttr << nameAttr;
        if (!e.imagePath.empty()) out << " src=\"" << e.imagePath << "\"";
        out << " />\n";
        return;
    }
    if (e.type == ElementType::Input) {
        out << pad << "<input" << idAttr << classAttr << typeAttr << nameAttr;
        if (!e.text.empty()) out << " placeholder=\"" << e.text << "\"";
        out << " />\n";
        return;
    }

    out << pad << "<" << tag << idAttr << classAttr << typeAttr << nameAttr << ">\n";

    if (!e.text.empty() && e.children.empty())
        out << pad << "  " << e.text << "\n";

    for (auto& child : e.children)
        exportElementWebHTML(*child, out, indent + 1);

    out << pad << "</" << tag << ">\n";
}

inline void appendWebCSSForElement(const UIElement& e, std::ostringstream& out,
                                   const Anchor& a, const Style& s, bool visible, bool important) {
    out << "#" << elementCssId(e) << " { " << buildElementCSS(a, s, visible, important) << "}\n";
}

inline void appendDefaultWebCSS(const UIElement& e, std::ostringstream& out) {
    appendWebCSSForElement(e, out, e.anchor, e.style, e.visible, false);
    for (auto& child : e.children)
        appendDefaultWebCSS(*child, out);
}

inline void appendBreakpointWebCSS(const UIElement& e, const std::string& bpName,
                                   std::ostringstream& out) {
    const BreakpointOverride* o = e.findOverride(bpName);
    if (o && (o->useAnchor || o->useStyle)) {
        const Anchor& a = o->useAnchor ? o->anchor : e.anchor;
        const Style& s = o->useStyle ? o->style : e.style;
        out << "  #" << elementCssId(e) << " { " << buildElementCSS(a, s, e.visible, true) << "}\n";
    }
    for (auto& child : e.children)
        appendBreakpointWebCSS(*child, bpName, out);
}

inline bool exportComponentPackage(const UIElement& root, const ComponentPackageInfo& info,
                                   ComponentTarget target, const std::filesystem::path& outDir,
                                   std::string* err = nullptr) {
    std::filesystem::path base = outDir;
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    if (ec) {
        if (err) *err = "Failed to create output directory";
        return false;
    }

    if (!writeTextFile(base / "manifest.json", buildManifest(info, target))) {
        if (err) *err = "Failed to write manifest";
        return false;
    }

    if (target == ComponentTarget::Web) {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
        html << "  <meta charset=\"UTF-8\">\n";
        html << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        html << "  <title>" << info.name << "</title>\n";
        html << "  <link rel=\"stylesheet\" href=\"styles.css\">\n";
        html << "</head>\n<body>\n";
        html << "<div id=\"myu-root\">\n";
        for (auto& child : root.children)
            exportElementWebHTML(*child, html, 1);
        html << "</div>\n";
        html << "<script src=\"script.js\"></script>\n";
        html << "</body>\n</html>\n";

        if (!writeTextFile(base / "index.html", html.str())) {
            if (err) *err = "Failed to write index.html";
            return false;
        }

        std::ostringstream css;
        css << "* { margin: 0; padding: 0; box-sizing: border-box; }\n";
        css << "body { background: #1a1a2e; overflow: hidden; }\n";
        css << "#myu-root { position: relative; width: " << info.canvasW << "px; height: "
            << info.canvasH << "px; margin: 0 auto; background: #16213e; }\n";
        for (auto& child : root.children)
            appendDefaultWebCSS(*child, css);

        for (auto& bp : info.breakpoints) {
            if (bp.width <= 0 || bp.height <= 0) continue;
            css << "@media (max-width: " << bp.width << "px) {\n";
            css << "  #myu-root { width: " << bp.width << "px; height: " << bp.height << "px; }\n";
            for (auto& child : root.children)
                appendBreakpointWebCSS(*child, bp.name, css);
            css << "}\n";
        }

        writeTextFile(base / "styles.css", css.str());
        writeTextFile(base / "script.js", "// Add behavior here\n");
        return true;
    }

    if (target == ComponentTarget::Xml) {
        std::string xml = exportToXML(root, info);
        if (!writeTextFile(base / "component.xml", xml)) {
            if (err) *err = "Failed to write component.xml";
            return false;
        }
        return true;
    }

    if (target == ComponentTarget::Desktop) {
        std::string json = exportToJSON(root);
        if (!writeTextFile(base / "layout.json", json)) {
            if (err) *err = "Failed to write layout.json";
            return false;
        }
        std::ostringstream h;
        std::ostringstream loader;
        loader << "#pragma once\n";
        loader << "#include <fstream>\n";
        loader << "#include <sstream>\n";
        loader << "#include <string>\n";
        loader << "\n";
        loader << "inline std::string loadComponentLayoutJson(const char* path) {\n";
        loader << "    std::ifstream f(path);\n";
        loader << "    if (!f) return {};\n";
        loader << "    std::ostringstream ss;\n";
        loader << "    ss << f.rdbuf();\n";
        loader << "    return ss.str();\n";
        loader << "}\n";
        writeTextFile(base / "ComponentLoader.h", loader.str());
        h << "#pragma once\n";
        h << "// Generated UI component header\n";
        h << "struct " << sanitizeName(info.name) << "Component {\n";
        h << "    static const char* layoutPath() { return \"layout.json\"; }\n";
        h << "};\n";
        if (!writeTextFile(base / "Component.h", h.str())) {
            if (err) *err = "Failed to write Component.h";
            return false;
        }
        std::ostringstream readme;
        readme << "This package includes layout.json exported from MyuEngine UI Designer.\n";
        readme << "Integrate it in your desktop app by loading the layout and mapping widgets.\n";
        writeTextFile(base / "README.txt", readme.str());
        return true;
    }

    if (err) *err = "Unknown target";
    return false;
}

} // namespace myu::ui
