/*
  Copyright (c) 2004-2010 The FlameRobin Development Team

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the
  "Software"), to deal in the Software without restriction, including
  without limitation the rights to use, copy, modify, merge, publish,
  distribute, sublicense, and/or sell copies of the Software, and to
  permit persons to whom the Software is furnished to do so, subject to
  the following conditions:

  The above copyright notice and this permission notice shall be included
  in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


  $Id$

*/

// For compilers that support precompilation, includes "wx/wx.h".
#include "wx/wxprec.h"

// for all others, include the necessary headers (this file is usually all you
// need because it includes almost all "standard" wxWindows headers
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#ifdef __BORLANDC__
    #pragma hdrstop
#endif

// needed for platform independent EOL
#include <wx/textbuf.h>

#include <string>

#include <boost/function.hpp>

#include <ibpp.h>

#include "core/FRError.h"
#include "core/StringUtils.h"
#include "engine/MetadataLoader.h"
#include "frutils.h"
#include "gui/AdvancedMessageDialog.h"
#include "metadata/domain.h"
#include "metadata/MetadataItemVisitor.h"
#include "metadata/parameter.h"
#include "metadata/procedure.h"
#include "sql/StatementBuilder.h"
//-----------------------------------------------------------------------------
Procedure::Procedure(DatabasePtr database, const wxString& name)
    : MetadataItem(ntProcedure, database.get(), name)
{
}
//-----------------------------------------------------------------------------
void Procedure::loadChildren()
{
    // in case an exception is thrown this should be repeated
    setChildrenLoaded(false);

    Database* d = getDatabase(wxT("Procedure::loadChildren"));
    MetadataLoader* loader = d->getMetadataLoader();
    // first start a transaction for metadata loading, then lock the procedure
    // when objects go out of scope and are destroyed, procedure will be
    // unlocked before the transaction is committed - any update() calls on
    // observers can possibly use the same transaction
    // when objects go out of scope and are destroyed, object will be unlocked
    // before the transaction is committed - any update() calls on observers
    // can possibly use the same transaction
    MetadataLoaderTransaction tr(loader);
    SubjectLocker lock(d);
    wxMBConv* converter = d->getCharsetConverter();

    std::string sql(
        "select p.rdb$parameter_name, p.rdb$field_source, "
        "p.rdb$parameter_type, "
    );
    if (d->getInfo().getODSVersionIsHigherOrEqualTo(11, 1))
        sql += "p.rdb$default_source, p.rdb$parameter_mechanism ";
    else
        sql += "null, -1 ";
    sql +=  "from rdb$procedure_parameters p "
            "where p.rdb$PROCEDURE_name = ? "
            "order by p.rdb$parameter_type, p.rdb$PARAMETER_number";

    IBPP::Statement st1 = loader->getStatement(sql);
    st1->Set(1, wx2std(getName_(), converter));
    st1->Execute();

    ParameterPtrs parameters;
    while (st1->Fetch())
    {
        std::string s;
        st1->Get(1, s);
        wxString param_name(std2wxIdentifier(s, converter));
        st1->Get(2, s);
        wxString source(std2wxIdentifier(s, converter));

        short partype, mechanism = -1;
        st1->Get(3, &partype);
        bool hasDefault = false;
        wxString defaultSrc;
        if (!st1->IsNull(4))
        {
            hasDefault = true;
            st1->Get(4, s);
            defaultSrc = std2wxIdentifier(s, converter);
        }
        if (!st1->IsNull(5))
            st1->Get(5, mechanism);

        ParameterPtr par = findParameter(param_name);
        if (!par)
        {
            par.reset(new Parameter(this, param_name));
            for (unsigned i = getLockCount(); i > 0; i--)
                par->lockSubject();
        }
        parameters.push_back(par);
        par->initialize(source, partype, mechanism, defaultSrc, hasDefault);
    }

    setChildrenLoaded(true);
    if (parametersM != parameters)
    {
        parametersM.swap(parameters);
        notifyObservers();
    }
}
//-----------------------------------------------------------------------------
bool Procedure::getChildren(std::vector<MetadataItem *>& temp)
{
    if (parametersM.empty())
        return false;
    std::transform(parametersM.begin(), parametersM.end(),
        std::back_inserter(temp), boost::mem_fn(&ParameterPtr::get));
    return true;
}
//-----------------------------------------------------------------------------
void Procedure::lockChildren()
{
    std::for_each(parametersM.begin(), parametersM.end(),
        boost::mem_fn(&Parameter::lockSubject));
}
//-----------------------------------------------------------------------------
void Procedure::unlockChildren()
{
    std::for_each(parametersM.begin(), parametersM.end(),
        boost::mem_fn(&Parameter::unlockSubject));
}
//-----------------------------------------------------------------------------
wxString Procedure::getExecuteStatement()
{
    ensureChildrenLoaded();

    wxArrayString columns, params;
    columns.Alloc(parametersM.size());
    params.Alloc(parametersM.size());

    for (ParameterPtrs::iterator it = parametersM.begin();
        it != parametersM.end(); ++it)
    {
        if ((*it)->isOutputParameter())
            columns.Add((*it)->getQuotedName());
        else
            params.Add((*it)->getQuotedName());
    }

    StatementBuilder sb;
    if (!columns.empty())
    {
        sb << kwSELECT << ' ' << StatementBuilder::IncIndent;

        // use "<<" only after concatenating everything
        // that shouldn't be split apart in line wrapping calculation
        for (size_t i = 0; i < columns.size() - 1; ++i)
            sb << wxT("p.") + columns[i] + wxT(", ");
        sb << wxT("p.") + columns.Last();

        sb << StatementBuilder::DecIndent << StatementBuilder::NewLine
            << kwFROM << ' ' << getQuotedName();
    }
    else
    {
        sb << kwEXECUTE << ' ' << kwPROCEDURE << ' ' << getQuotedName();
    }

    if (!params.empty())
    {
        sb << wxT(" (") << StatementBuilder::IncIndent;

        // use "<<" only after concatenating everything
        // that shouldn't be split apart in line wrapping calculation
        for (size_t i = 0; i < params.size() - 1; ++i)
            sb << params[i] + wxT(", ");
        sb << params.Last() + wxT(")");

        sb << StatementBuilder::DecIndent;
    }
    
    if (!columns.empty())
        sb << wxT(" p");

    return sb;
}
//-----------------------------------------------------------------------------
ParameterPtrs::iterator Procedure::begin()
{
    // please - don't load here
    // this code is used to get columns we want to alert about changes
    // but if there aren't any columns, we don't want to waste time
    // loading them
    return parametersM.begin();
}
//-----------------------------------------------------------------------------
ParameterPtrs::iterator Procedure::end()
{
    // please see comment for begin()
    return parametersM.end();
}
//-----------------------------------------------------------------------------
ParameterPtrs::const_iterator Procedure::begin() const
{
    return parametersM.begin();
}
//-----------------------------------------------------------------------------
ParameterPtrs::const_iterator Procedure::end() const
{
    return parametersM.end();
}
//-----------------------------------------------------------------------------
ParameterPtr Procedure::findParameter(const wxString& name) const
{
    for (ParameterPtrs::const_iterator it = parametersM.begin();
        it != parametersM.end(); ++it)
    {
        if ((*it)->getName_() == name)
            return *it;
    }
    return ParameterPtr();
}
//-----------------------------------------------------------------------------
size_t Procedure::getParamCount() const
{
    return parametersM.size();
}
//-----------------------------------------------------------------------------
wxString Procedure::getOwner()
{
    Database* d = getDatabase(wxT("Procedure::getOwner"));
    MetadataLoader* loader = d->getMetadataLoader();
    MetadataLoaderTransaction tr(loader);

    IBPP::Statement st1 = loader->getStatement(
        "select rdb$owner_name from rdb$procedures where rdb$procedure_name = ?");
    st1->Set(1, wx2std(getName_(), d->getCharsetConverter()));
    st1->Execute();
    st1->Fetch();
    std::string name;
    st1->Get(1, name);
    return std2wxIdentifier(name, d->getCharsetConverter());
}
//-----------------------------------------------------------------------------
wxString Procedure::getSource()
{
    Database* d = getDatabase(wxT("Procedure::getSource"));
    MetadataLoader* loader = d->getMetadataLoader();
    MetadataLoaderTransaction tr(loader);

    IBPP::Statement st1 = loader->getStatement(
        "select rdb$procedure_source from rdb$procedures where rdb$procedure_name = ?");
    st1->Set(1, wx2std(getName_(), d->getCharsetConverter()));
    st1->Execute();
    st1->Fetch();
    wxString source;
    readBlob(st1, 1, source, d->getCharsetConverter());
    source.Trim(false);     // remove leading whitespace
    return source;
}
//-----------------------------------------------------------------------------
wxString Procedure::getDefinition()
{
    ensureChildrenLoaded();
    wxString collist, parlist;
    ParameterPtrs::const_iterator lastInput, lastOutput;
    for (ParameterPtrs::const_iterator it = parametersM.begin();
        it != parametersM.end(); ++it)
    {
        if ((*it)->isOutputParameter())
            lastOutput = it;
        else
            lastInput = it;
    }
    for (ParameterPtrs::const_iterator it =
        parametersM.begin(); it != parametersM.end(); ++it)
    {
        // No need to quote domains, as currently only regular datatypes can be
        // used for SP parameters
        if ((*it)->isOutputParameter())
        {
            collist += wxT("    ") + (*it)->getQuotedName() + wxT(" ")
                + (*it)->getDomain()->getDatatypeAsString();
            if (it != lastOutput)
                collist += wxT(",");
            collist += wxT("\n");
        }
        else
        {
            parlist += wxT("    ") + (*it)->getQuotedName() + wxT(" ")
                + (*it)->getDomain()->getDatatypeAsString();
            if (it != lastInput)
                parlist += wxT(",");
            parlist += wxT("\n");
        }
    }
    wxString retval = getQuotedName();
    if (!parlist.empty())
        retval += wxT("(\n") + parlist + wxT(")");
    retval += wxT("\n");
    if (!collist.empty())
        retval += wxT("returns:\n") + collist;
    return retval;
}
//-----------------------------------------------------------------------------
wxString Procedure::getAlterSql(bool full)
{
    ensureChildrenLoaded();

    Database* db = getDatabase(wxT("Procedure::getAlterSql"));

    wxString sql = wxT("SET TERM ^ ;\nALTER PROCEDURE ") + getQuotedName();
    if (!parametersM.empty())
    {
        wxString input, output;
        for (ParameterPtrs::const_iterator it = parametersM.begin();
            it != parametersM.end(); ++it)
        {
            wxString charset;
            wxString param = (*it)->getQuotedName() + wxT(" ");
            Domain* dm = (*it)->getDomain();
            if (dm)
            {
                if (dm->isSystem()) // autogenerated domain -> use datatype
                {
                    param += dm->getDatatypeAsString();
                    charset = dm->getCharset();
                    if (!charset.IsEmpty())
                    {
                        if (charset != db->getDatabaseCharset())
                            charset = wxT(" CHARACTER SET ") + charset;
                        else
                            charset = wxT("");
                    }
                }
                else
                {
                    if ((*it)->getMechanism() == 1)
                        param += wxT("TYPE OF ") + dm->getQuotedName();
                    else
                        param += dm->getQuotedName();
                }
            }
            else
                param += (*it)->getSource();

            if ((*it)->isOutputParameter())
            {
                if (output.empty())
                    output += wxT("\nRETURNS (\n    ");
                else
                    output += wxT(",\n    ");
                output += param + charset;
            }
            else
            {
                if (input.empty())
                    input += wxT(" (\n    ");
                else
                    input += wxT(",\n    ");
                input += param;
                if ((*it)->hasDefault())
                    input += wxT(" ") + (*it)->getDefault();
                input += charset;
            }
        }

        if (!input.empty())
            sql += input + wxT(" )");
        if (!output.empty())
            sql += output + wxT(" )");
    }
    sql += wxT("\nAS\n");
    if (full)
        sql += getSource();
    else
        sql += wxT("BEGIN SUSPEND; END");
    sql += wxT("^\nSET TERM ; ^\n");
    return sql;
}
//-----------------------------------------------------------------------------
void Procedure::checkDependentProcedures()
{
    // check dependencies and parameters
    ensureChildrenLoaded();
    std::vector<Dependency> deps;
    getDependencies(deps, false);

    // if there is a dependency, but parameter doesn't exist, warn the user
    int count = 0;
    wxString missing;
    for (std::vector<Dependency>::iterator it = deps.begin();
        it != deps.end(); ++it)
    {
        std::vector<wxString> fields;
        (*it).getFields(fields);
        for (std::vector<wxString>::const_iterator ci = fields.begin();
            ci != fields.end(); ++ci)
        {
            bool found = false;
            for (ParameterPtrs::iterator i2 = begin();
                i2 != end(); ++i2)
            {
                if ((*i2)->getName_() == (*ci))
                {
                    found = true;
                    break;
                }
            }
            if (!found && ++count < 20)
            {
                missing += wxString::Format(
                    _("Procedure %s depends on parameter %s.%s"),
                    (*it).getName_().c_str(),
                    (*ci).c_str(),
                    wxTextBuffer::GetEOL()
                );
            }
        }
    }
    if (count > 0)
    {
        if (count > 19)
        {
            missing += wxTextBuffer::GetEOL()
            + wxString::Format(_("%d total dependencies (20 shown)."), count);
        }
        showWarningDialog(0,
            _("Dependencies broken"),
            wxString::Format(
                _("Some other procedures depend on %s:%s%s%s"),
                getName_().c_str(),
                wxTextBuffer::GetEOL(),
                wxTextBuffer::GetEOL(),
                missing.c_str()),
            AdvancedMessageDialogButtonsOk()
        );
    }
}
//-----------------------------------------------------------------------------
std::vector<Privilege>* Procedure::getPrivileges()
{
    // load privileges from database and return the pointer to collection
    Database* d = getDatabase(wxT("Procedure::getPrivileges"));
    MetadataLoader* loader = d->getMetadataLoader();
    // first start a transaction for metadata loading, then lock the procedure
    // when objects go out of scope and are destroyed, procedure will be
    // unlocked before the transaction is committed - any update() calls on
    // observers can possibly use the same transaction
    MetadataLoaderTransaction tr(loader);
    SubjectLocker lock(this);
    wxMBConv* converter = d->getCharsetConverter();

    privilegesM.clear();

    IBPP::Statement st1 = loader->getStatement(
        "select RDB$USER, RDB$USER_TYPE, RDB$GRANTOR, RDB$PRIVILEGE, "
        "RDB$GRANT_OPTION, RDB$FIELD_NAME "
        "from RDB$USER_PRIVILEGES "
        "where RDB$RELATION_NAME = ? and rdb$object_type = 5 "
        "order by rdb$user, rdb$user_type, rdb$privilege"
    );
    st1->Set(1, wx2std(getName_(), converter));
    st1->Execute();
    std::string lastuser;
    int lasttype = -1;
    Privilege *pr = 0;
    while (st1->Fetch())
    {
        std::string user, grantor, privilege, field;
        int usertype, grantoption = 0;
        st1->Get(1, user);
        st1->Get(2, usertype);
        st1->Get(3, grantor);
        st1->Get(4, privilege);
        if (!st1->IsNull(5))
            st1->Get(5, grantoption);
        st1->Get(6, field);
        if (!pr || user != lastuser || usertype != lasttype)
        {
            Privilege p(this, std2wxIdentifier(user, converter),
                usertype);
            privilegesM.push_back(p);
            pr = &privilegesM.back();
            lastuser = user;
            lasttype = usertype;
        }
        pr->addPrivilege(privilege[0], std2wx(grantor, converter),
            grantoption == 1);
    }
    return &privilegesM;
}
//-----------------------------------------------------------------------------
const wxString Procedure::getTypeName() const
{
    return wxT("PROCEDURE");
}
//-----------------------------------------------------------------------------
void Procedure::acceptVisitor(MetadataItemVisitor* visitor)
{
    visitor->visitProcedure(*this);
}
//-----------------------------------------------------------------------------
// Procedures collection
Procedures::Procedures(DatabasePtr database)
    : MetadataCollection<Procedure>(ntProcedures, database, _("Procedures"))
{
}
//-----------------------------------------------------------------------------
void Procedures::acceptVisitor(MetadataItemVisitor* visitor)
{
    visitor->visitProcedures(*this);
}
//-----------------------------------------------------------------------------
void Procedures::load(ProgressIndicator* progressIndicator)
{
    wxString stmt = wxT("select rdb$procedure_name from rdb$procedures")
        wxT(" where (rdb$system_flag = 0 or rdb$system_flag is null)")
        wxT(" order by 1");
    setItems(getDatabase()->loadIdentifiers(stmt, progressIndicator));
}
//-----------------------------------------------------------------------------
void Procedures::loadChildren()
{
    load(0);
}
//-----------------------------------------------------------------------------
