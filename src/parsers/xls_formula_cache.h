#pragma once
#include "paleo_data/parser.h"
#include <QString>
namespace paleo::data {
// FreeXL intentionally ignores BIFF FORMULA records. Supplement only cached
// results; never evaluate formulas or alter source workbooks.
bool supplementXlsFormulaCells(const QString&,ParsedFile&,const CancelCheck&,QString&);
}
