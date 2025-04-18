/////////////////////////////////////////////////////////////////////////////
// Name:        src/html/winpars.cpp
// Purpose:     wxHtmlParser class (generic parser)
// Author:      Vaclav Slavik
// Copyright:   (c) 1999 Vaclav Slavik
// Licence:     wxWindows licence
/////////////////////////////////////////////////////////////////////////////

#include "wx/wxprec.h"


#if wxUSE_HTML && wxUSE_STREAMS

#ifndef WX_PRECOMP
    #include "wx/intl.h"
    #include "wx/dc.h"
    #include "wx/log.h"
    #include "wx/settings.h"
#endif

#include "wx/html/htmldefs.h"
#include "wx/html/winpars.h"
#include "wx/html/htmlwin.h"
#include "wx/html/styleparams.h"
#include "wx/fontmap.h"
#include "wx/uri.h"

#include "wx/private/hyperlink.h"

//-----------------------------------------------------------------------------
// wxHtmlWinParser
//-----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(wxHtmlWinParser, wxHtmlParser);

wxList wxHtmlWinParser::m_Modules;

wxHtmlWinParser::wxHtmlWinParser(wxHtmlWindowInterface *wndIface)
{
    m_windowInterface = wndIface;
    m_Container = nullptr;
    m_DC = nullptr;
    m_CharHeight = m_CharWidth = 0;
    m_UseLink = false;
    m_whitespaceMode = Whitespace_Normal;
    m_lastWordCell = nullptr;
    m_posColumn = 0;

    {
        int i, j, k, l, m;
        for (i = 0; i < 2; i++)
            for (j = 0; j < 2; j++)
                for (k = 0; k < 2; k++)
                    for (l = 0; l < 2; l++)
                        for (m = 0; m < 7; m++)
                        {
                            m_FontsTable[i][j][k][l][m] = nullptr;
                        }

        SetFonts(wxEmptyString, wxEmptyString, nullptr);
    }

    // fill in wxHtmlParser's tables:
    wxList::compatibility_iterator node = m_Modules.GetFirst();
    while (node)
    {
        wxHtmlTagsModule *mod = (wxHtmlTagsModule*) node->GetData();
        mod->FillHandlersTable(this);
        node = node->GetNext();
    }
}

wxHtmlWinParser::~wxHtmlWinParser()
{
    int i, j, k, l, m;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++)
                for (l = 0; l < 2; l++)
                    for (m = 0; m < 7; m++)
                    {
                        if (m_FontsTable[i][j][k][l][m] != nullptr)
                            delete m_FontsTable[i][j][k][l][m];
                    }
}

void wxHtmlWinParser::AddModule(wxHtmlTagsModule *module)
{
    m_Modules.Append(module);
}

void wxHtmlWinParser::RemoveModule(wxHtmlTagsModule *module)
{
    m_Modules.DeleteObject(module);
}

// build all HTML font sizes (1..7) from the given base size
static void wxBuildFontSizes(int *sizes, int size)
{
    // using a fixed factor (1.2, from CSS2) is a bad idea as explained at
    // http://www.w3.org/TR/CSS21/fonts.html#font-size-props but this is by far
    // simplest thing to do so still do it like this for now
    sizes[0] = int(size * 0.75); // exception to 1.2 rule, otherwise too small
    sizes[1] = int(size * 0.83);
    sizes[2] = size;
    sizes[3] = int(size * 1.2);
    sizes[4] = int(size * 1.44);
    sizes[5] = int(size * 1.73);
    sizes[6] = int(size * 2);
}

static int wxGetDefaultHTMLFontSize()
{
    // base the default font size on the size of the default system font but
    // also ensure that we have a font of reasonable size, otherwise small HTML
    // fonts are unreadable
    int size = wxNORMAL_FONT->GetPointSize();
    if ( size < 10 )
        size = 10;
    return size;
}

void wxHtmlWinParser::SetFonts(const wxString& normal_face,
                               const wxString& fixed_face,
                               const int *sizes)
{
    static int default_sizes[7] = { 0 };
    if ( !sizes )
    {
        if ( !default_sizes[0] )
            wxBuildFontSizes(default_sizes, wxGetDefaultHTMLFontSize());

        sizes = default_sizes;
    }

    int i, j, k, l, m;

    for (i = 0; i < 7; i++)
        m_FontsSizes[i] = sizes[i];

    m_FontFaceFixed = fixed_face;
    m_FontFaceNormal = normal_face;

    for (i = 0; i < 2; i++)
        for (j = 0; j < 2; j++)
            for (k = 0; k < 2; k++)
                for (l = 0; l < 2; l++)
                    for (m = 0; m < 7; m++) {
                        if (m_FontsTable[i][j][k][l][m] != nullptr)
                        {
                            delete m_FontsTable[i][j][k][l][m];
                            m_FontsTable[i][j][k][l][m] = nullptr;
                        }
                    }
}

void wxHtmlWinParser::SetStandardFonts(int size,
                                       const wxString& normal_face,
                                       const wxString& fixed_face)
{
    if (size == -1)
        size = wxGetDefaultHTMLFontSize();

    int f_sizes[7];
    wxBuildFontSizes(f_sizes, size);

    wxString normal = normal_face;
    if ( normal.empty() )
        normal = wxNORMAL_FONT->GetFaceName();

    SetFonts(normal, fixed_face, f_sizes);
}

void wxHtmlWinParser::InitParser(const wxString& source)
{
    wxHtmlParser::InitParser(source);
    wxASSERT_MSG(m_DC != nullptr, wxT("no DC assigned to wxHtmlWinParser!!"));

    m_FontBold = m_FontItalic = m_FontUnderlined = m_FontFixed = FALSE;
    m_FontSize = 3; //default one
    CreateCurrentFont();           // we're selecting default font into

    // we're not using GetCharWidth/Height() because of
    // differences under X and win
    wxCoord w,h;
    m_DC->GetTextExtent( wxT("H"), &w, &h);
    m_CharWidth = w;
    m_CharHeight = h;

    m_UseLink = false;
    m_Link = wxHtmlLinkInfo( wxEmptyString );
    m_LinkColor = wxPrivate::GetLinkColour();

    // if an HTML window interface is connected to this parser,
    // then use its control background color and the system
    // default text color.
    if ( m_windowInterface )
    {
        m_ActualColor = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
        m_ActualBackgroundColor = m_windowInterface->GetHTMLBackgroundColour();
    }
    // Otherwise, no window interface is connected to this parser
    // (e.g., an HTML renderer being used for printing).
    // Fall back to the default white background and black text.
    else
    {
        m_ActualColor = *wxBLACK;
        m_ActualBackgroundColor = *wxWHITE;
    }

    m_ActualBackgroundMode = wxBRUSHSTYLE_TRANSPARENT;
    m_Align = wxHTML_ALIGN_LEFT;
    m_ScriptMode = wxHTML_SCRIPT_NORMAL;
    m_ScriptBaseline = 0;
    m_tmpLastWasSpace = false;
    m_lastWordCell = nullptr;

    // open the toplevel container that contains everything else and that
    // is never closed (this makes parser's life easier):
    OpenContainer();

    // then open the first container into which page's content will go:
    OpenContainer();

    m_Container->InsertCell(new wxHtmlColourCell(m_ActualColor));

    m_Container->InsertCell
                 (
                   new wxHtmlColourCell
                       (
                         m_ActualBackgroundColor,
                         m_ActualBackgroundMode == wxBRUSHSTYLE_TRANSPARENT ? wxHTML_CLR_TRANSPARENT_BACKGROUND : wxHTML_CLR_BACKGROUND
                       )
                  );

    m_Container->InsertCell(new wxHtmlFontCell(CreateCurrentFont()));
}

void wxHtmlWinParser::DoneParser()
{
    m_Container = nullptr;
    wxHtmlParser::DoneParser();
}

wxObject* wxHtmlWinParser::GetProduct()
{
    wxHtmlContainerCell *top;

    CloseContainer();
    OpenContainer();

    top = m_Container;
    while (top->GetParent()) top = top->GetParent();
    top->RemoveExtraSpacing(true, true);

    return top;
}

wxFSFile *wxHtmlWinParser::OpenURL(wxHtmlURLType type,
                                   const wxString& url) const
{
    if ( !m_windowInterface )
        return wxHtmlParser::OpenURL(type, url);

    wxString myurl(url);
    wxHtmlOpeningStatus status;
    for (;;)
    {
        wxString myfullurl(myurl);

        // consider url as absolute path first
        wxURI current(myurl);
        myfullurl = current.BuildUnescapedURI();

        // if not absolute then ...
        if( current.IsRelative() )
        {
            wxString basepath = GetFS()->GetPath();
            wxURI base(basepath);

            // ... try to apply base path if valid ...
            if( !base.IsReference() )
            {
                wxURI path(myfullurl);
                path.Resolve( base );
                myfullurl = path.BuildUnescapedURI();
            }
            else
            {
                // ... or force such addition if not included already
                if( !current.GetPath().Contains(base.GetPath()) )
                {
                    basepath += myurl;
                    wxURI connected( basepath );
                    myfullurl = connected.BuildUnescapedURI();
                }
            }
        }

        wxString redirect;
        status = m_windowInterface->OnHTMLOpeningURL(type, myfullurl, &redirect);
        if ( status != wxHTML_REDIRECT )
            break;

        myurl = redirect;
    }

    if ( status == wxHTML_BLOCK )
        return nullptr;

    int flags = wxFS_READ;
    if (type == wxHTML_URL_IMAGE)
        flags |= wxFS_SEEKABLE;

    return GetFS()->OpenFile(myurl, flags);
}

static constexpr wxChar CUR_NBSP_VALUE = L'\xA0';

void wxHtmlWinParser::AddText(const wxString& txt)
{
    if ( m_whitespaceMode == Whitespace_Normal )
    {
        m_tmpStrBuf.reserve(txt.length());
        m_tmpStrBuf.clear();

        wxString::const_iterator i = txt.begin();
        const wxString::const_iterator end = txt.end();

        if (m_tmpLastWasSpace)
        {
            while ( (i < end) &&
                    (*i == wxT('\n') || *i == wxT('\r') || *i == wxT(' ') ||
                     *i == wxT('\t')) )
            {
                ++i;
            }
        }

        while (i < end)
        {
            size_t x = 0;
            const wxUniChar d = *i;
            if ((d == wxT('\n')) || (d == wxT('\r')) || (d == wxT(' ')) || (d == wxT('\t')))
            {
                ++i;
                ++x;
                while ( (i < end) &&
                        (*i == wxT('\n') || *i == wxT('\r') ||
                         *i == wxT(' ') || *i == wxT('\t')) )
                {
                    ++i;
                    ++x;
                }
            }
            else
            {
                ++i;
            }

            if (x)
            {
                m_tmpStrBuf.append(' ');
                m_tmpStrBuf.Replace(CUR_NBSP_VALUE, ' ');
                AddWord(m_tmpStrBuf);
                m_tmpStrBuf.clear();

                m_tmpLastWasSpace = true;
            }
            else
            {
                m_tmpStrBuf.append(d);
            }
        }

        switch (m_tmpStrBuf.length())
        {
            case 1:
                // If the only remaining character is space, don't add it.
                if (m_tmpStrBuf[0] == wxT(' '))
                    break;
                wxFALLTHROUGH;

            default:
                AddWord(m_tmpStrBuf);
                m_tmpStrBuf.clear();
                m_tmpLastWasSpace = false;
                break;

            case 0:
                break;
        }
    }
    else // m_whitespaceMode == Whitespace_Pre
    {
        if ( txt.find(CUR_NBSP_VALUE) != wxString::npos )
        {
            // we need to substitute spaces for &nbsp; here just like we
            // did in the Whitespace_Normal branch above
            wxString txt2(txt);
            txt2.Replace(CUR_NBSP_VALUE, ' ');
            AddPreBlock(txt2);
        }
        else
        {
            AddPreBlock(txt);
        }

        // don't eat any whitespace in <pre> block
        m_tmpLastWasSpace = false;
    }
}

void wxHtmlWinParser::AddWord(wxHtmlWordCell *word)
{
    ApplyStateToCell(word);

    m_Container->InsertCell(word);
    word->SetPreviousWord(m_lastWordCell);
    m_lastWordCell = word;
}

void wxHtmlWinParser::AddPreBlock(const wxString& text)
{
    if ( text.find('\t') != wxString::npos )
    {
        wxString text2;
        text2.reserve(text.length());

        const wxString::const_iterator end = text.end();
        wxString::const_iterator copyFrom = text.begin();
        size_t pos = 0;
        int posColumn = m_posColumn;
        for ( wxString::const_iterator i = copyFrom; i != end; ++i, ++pos )
        {
            if ( *i == '\t' )
            {
                if ( copyFrom != i )
                    text2.append(copyFrom, i);

                const unsigned SPACES_PER_TAB = 8;
                const size_t expandTo = SPACES_PER_TAB - posColumn % SPACES_PER_TAB;
                text2.append(expandTo, ' ');

                posColumn += expandTo;
                copyFrom = i + 1;
            }
            else
            {
                ++posColumn;
            }
        }
        if ( copyFrom != text.end() )
            text2.append(copyFrom, text.end());

        AddWord(new wxHtmlWordWithTabsCell(text2, text, m_posColumn, *(GetDC())));

        m_posColumn = posColumn;
    }
    else
    {
        // no special formatting needed
        AddWord(text);
        m_posColumn += text.length();
    }
}


wxHtmlContainerCell* wxHtmlWinParser::OpenContainer()
{
    m_Container = new wxHtmlContainerCell(m_Container);
    m_Container->SetAlignHor(m_Align);
    m_posColumn = 0;
    m_tmpLastWasSpace = true;
        /* to avoid space being first character in paragraph */
    return m_Container;
}



wxHtmlContainerCell* wxHtmlWinParser::SetContainer(wxHtmlContainerCell *c)
{
    m_tmpLastWasSpace = true;
        /* to avoid space being first character in paragraph */
    return m_Container = c;
}



wxHtmlContainerCell* wxHtmlWinParser::CloseContainer()
{
    m_Container = m_Container->GetParent();
    return m_Container;
}


void wxHtmlWinParser::SetFontSize(int s)
{
    if (s < 1)
        s = 1;
    else if (s > 7)
        s = 7;
    m_FontSize = s;
}


void wxHtmlWinParser::SetDC(wxDC *dc, double pixel_scale, double font_scale)
{
    m_DC = dc;
    m_PixelScale = pixel_scale;
    m_FontScale = font_scale;
}

void wxHtmlWinParser::SetFontPointSize(int pt)
{
    if (pt <= m_FontsSizes[0])
        m_FontSize = 1;
    else if (pt >= m_FontsSizes[6])
        m_FontSize = 7;
    else
    {
        // Find the font closest to the given value with a simple linear search
        // (binary search is not worth it here for so small number of elements)
        for ( int n = 0; n < 6; n++ )
        {
            if ( (pt > m_FontsSizes[n]) && (pt <= m_FontsSizes[n + 1]) )
            {
                if ( (pt - m_FontsSizes[n]) >= (m_FontsSizes[n + 1] - pt) )
                {
                    // The actual size is closer to the next entry than to this
                    // one, so use it.
                    n++;
                }

                // Notice that m_FontSize starts from 1, hence +1 here.
                m_FontSize = n + 1;

                break;
            }
        }
    }
}

wxFont* wxHtmlWinParser::CreateCurrentFont()
{
    int fb = GetFontBold(),
        fi = GetFontItalic(),
        fu = GetFontUnderlined(),
        ff = GetFontFixed(),
        fs = GetFontSize() - 1 /*remap from <1;7> to <0;6>*/ ;

    wxString face = ff ? m_FontFaceFixed : m_FontFaceNormal;
    wxString *faceptr = &(m_FontsFacesTable[fb][fi][fu][ff][fs]);
    wxFont **fontptr = &(m_FontsTable[fb][fi][fu][ff][fs]);

    if (*fontptr != nullptr && (*faceptr != face))
    {
        wxDELETE(*fontptr);
    }

    if (*fontptr == nullptr)
    {
        *faceptr = face;
        *fontptr = new wxFont(
                       (int) (m_FontsSizes[fs] * m_FontScale),
                       ff ? wxFONTFAMILY_MODERN : wxFONTFAMILY_SWISS,
                       fi ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL,
                       fb ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
                       fu ? true : false, face
                       );
    }
    m_DC->SetFont(**fontptr);
    return (*fontptr);
}



void wxHtmlWinParser::SetLink(const wxHtmlLinkInfo& link)
{
    m_Link = link;
    m_UseLink = !link.GetHref().empty();
}

void wxHtmlWinParser::SetFontFace(const wxString& face)
{
    if (GetFontFixed())
        m_FontFaceFixed = face;
    else
        m_FontFaceNormal = face;
}

void wxHtmlWinParser::ApplyStateToCell(wxHtmlCell *cell)
{
    // set the link:
    if (m_UseLink)
        cell->SetLink(GetLink());

    // apply current script mode settings:
    cell->SetScriptMode(GetScriptMode(), GetScriptBaseline());
}


//-----------------------------------------------------------------------------
// wxHtmlWinTagHandler
//-----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(wxHtmlWinTagHandler, wxHtmlTagHandler);

void wxHtmlWinTagHandler::ApplyStyle(const wxHtmlStyleParams &styleParams)
{
    wxString str;

    str = styleParams.GetParam(wxS("color"));
    if ( !str.empty() )
    {
        wxColour clr;
        if ( wxHtmlTag::ParseAsColour(str, &clr) )
        {
            m_WParser->SetActualColor(clr);
            m_WParser->GetContainer()->InsertCell(new wxHtmlColourCell(clr));
        }
    }

    str = styleParams.GetParam(wxS("background-color"));
    if ( !str.empty() )
    {
        wxColour clr;
        if ( wxHtmlTag::ParseAsColour(str, &clr) )
        {
            m_WParser->SetActualBackgroundColor(clr);
            m_WParser->SetActualBackgroundMode(wxBRUSHSTYLE_SOLID);
            m_WParser->GetContainer()->InsertCell(new wxHtmlColourCell(clr, wxHTML_CLR_BACKGROUND));
        }
    }

    str = styleParams.GetParam(wxS("font-size"));
    if ( !str.empty() )
    {
        // Point size
        int foundIndex = str.Find(wxS("pt"));
        if (foundIndex != wxNOT_FOUND)
        {
            str.Truncate(foundIndex);

            long sizeValue;
            if (str.ToLong(&sizeValue) == true)
            {
                // Set point size
                m_WParser->SetFontPointSize(sizeValue);
                m_WParser->GetContainer()->InsertCell(
                    new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
            }
        }
        // else: check for other ways of specifying size (TODO)
    }

    str = styleParams.GetParam(wxS("font-weight"));
    if ( !str.empty() )
    {
        // Only bold and normal supported just now
        if ( str == wxS("bold") )
        {
            m_WParser->SetFontBold(true);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
        else if ( str == wxS("normal") )
        {
            m_WParser->SetFontBold(false);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
    }

    str = styleParams.GetParam(wxS("font-style"));
    if ( !str.empty() )
    {
        // "oblique" and "italic" are more or less the same.
        // "inherit" (using the parent font) is not supported.
        if ( str == wxS("oblique") || str == wxS("italic") )
        {
            m_WParser->SetFontItalic(true);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
        else if ( str == wxS("normal") )
        {
            m_WParser->SetFontItalic(false);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
    }

    str = styleParams.GetParam(wxS("text-decoration"));
    if ( !str.empty() )
    {
        // Only underline is supported.
        if ( str == wxS("underline") )
        {
            m_WParser->SetFontUnderlined(true);
            m_WParser->GetContainer()->InsertCell(
                new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
        }
    }

    str = styleParams.GetParam(wxS("font-family"));
    if ( !str.empty() )
    {
        m_WParser->SetFontFace(str);
        m_WParser->GetContainer()->InsertCell(
            new wxHtmlFontCell(m_WParser->CreateCurrentFont()));
    }
}

//-----------------------------------------------------------------------------
// wxHtmlTagsModule
//-----------------------------------------------------------------------------

// NB: This is *NOT* winpars.cpp's initialization and shutdown code!!
//     This module is an ancestor for tag handlers modules defined
//     in m_*.cpp files with TAGS_MODULE_BEGIN...TAGS_MODULE_END construct.
//
//     Do not add any winpars.cpp shutdown or initialization code to it,
//     create a new module instead!

wxIMPLEMENT_DYNAMIC_CLASS(wxHtmlTagsModule, wxModule);

bool wxHtmlTagsModule::OnInit()
{
    wxHtmlWinParser::AddModule(this);
    return true;
}

void wxHtmlTagsModule::OnExit()
{
    wxHtmlWinParser::RemoveModule(this);
}

#endif
