/*
  The contents of this file are subject to the Initial Developer's Public
  License Version 1.0 (the "License"); you may not use this file except in
  compliance with the License. You may obtain a copy of the License here:
  http://www.flamerobin.org/license.html.

  Software distributed under the License is distributed on an "AS IS"
  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See the
  License for the specific language governing rights and limitations under
  the License.

  The Original Code is FlameRobin (TM).

  The Initial Developer of the Original Code is Milan Babuskov.

  Portions created by the original developer
  are Copyright (C) 2004 Milan Babuskov.

  All Rights Reserved.

  $Id$

  Contributor(s): Nando Dessena.
*/

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWindows headers
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif
//-----------------------------------------------------------------------------

#include "core/FRError.h"
#include "dberror.h"
#include "gui/MultilineEnterDialog.h"
#include "metadata/database.h"
#include "metadata/metadataitem.h"
#include "ugly.h"
#include "urihandler.h"

class ObjectDescriptionHandler: public URIHandler
{
public:
    bool handleURI(URI& uri);
private:
    // singleton; registers itself on creation.
    static const ObjectDescriptionHandler handlerInstance;
};
//-----------------------------------------------------------------------------
const ObjectDescriptionHandler ObjectDescriptionHandler::handlerInstance;
//-----------------------------------------------------------------------------
bool ObjectDescriptionHandler::handleURI(URI& uri)
{
    if (uri.action != wxT("edit_description"))
        return false;

    FR_TRY

    MetadataItem* m = (MetadataItem*)getObject(uri);
    wxWindow* w = getWindow(uri);
    if (!m || !w)
        return true;

    wxString desc = m->getDescription();
    if (GetMultilineTextFromUser(w, wxString::Format(_("Description of %s"),
        uri.getParam(wxT("object_name")).c_str()), desc))
    {
        wxBusyCursor wait;
        m->setDescription(desc);
        // FIXME: This can be removed when MetadataItem::setDescriptionM() 
        //        is fixed to call it without recursion.
        m->notifyObservers();
    }

    FR_CATCH

    return true;
}
//-----------------------------------------------------------------------------

