/** @file tfidfweight.cc
 * @brief Xapian::TfIdfWeight class - The TfIdf weighting scheme
 */
/* Copyright (C) 2013 Aarsh Shah
 * Copyright (C) 2016 Vivek Pal
 * Copyright (C) 2016,2017 Olly Betts
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <config.h>

#include "xapian/weight.h"
#include <cmath>
#include <cstring>

#include "debuglog.h"
#include "omassert.h"
#include "serialise-double.h"

#include "xapian/error.h"

using namespace std;

namespace Xapian {

TfIdfWeight::TfIdfWeight(const std::string& normals)
    : TfIdfWeight::TfIdfWeight(normals, 0.2, 1.0) {}

TfIdfWeight::TfIdfWeight(const std::string& normals, double slope, double delta)
    : param_slope(slope), param_delta(delta)
{
    if (normals.length() != 3 ||
	!strchr("nbslPL", normals[0]) ||
	!strchr("ntpfsP", normals[1]) ||
	!strchr("n", normals[2]))
	throw Xapian::InvalidArgumentError("Normalization string is invalid");
    if (param_slope <= 0)
	throw Xapian::InvalidArgumentError("Parameter slope is invalid");
    if (param_delta <= 0)
	throw Xapian::InvalidArgumentError("Parameter delta is invalid");
    if (normals[1] != 'n') {
	need_stat(TERMFREQ);
	need_stat(COLLECTION_SIZE);
    }
    need_stat(WDF);
    need_stat(WDF_MAX);
    need_stat(WQF);
    if (normals[0] == 'P' || normals[1] == 'P') {
	need_stat(AVERAGE_LENGTH);
	need_stat(DOC_LENGTH);
	need_stat(DOC_LENGTH_MIN);
    }
    if (normals[0] == 'L') {
	need_stat(DOC_LENGTH);
	need_stat(DOC_LENGTH_MIN);
	need_stat(DOC_LENGTH_MAX);
	need_stat(UNIQUE_TERMS);
    }
    switch (normals[0]) {
	case 'b':
	    wdf_norm = WDF_NORM::BOOLEAN;
	    break;
	case 's':
	    wdf_norm = WDF_NORM::SQUARE;
	    break;
	case 'l':
	    wdf_norm = WDF_NORM::LOG;
	    break;
	case 'P':
	    wdf_norm = WDF_NORM::PIVOTED;
	    break;
	case 'L':
	    wdf_norm = WDF_NORM::LOG_AVERAGE;
	    break;
	default:
	    wdf_norm = WDF_NORM::NONE;
    }
    switch (normals[1]) {
	case 'n':
	    idf_norm = IDF_NORM::NONE;
	    break;
	case 's':
	    idf_norm = IDF_NORM::SQUARE;
	    break;
	case 'f':
	    idf_norm = IDF_NORM::FREQ;
	    break;
	case 'P':
	    idf_norm = IDF_NORM::PIVOTED;
	    break;
	case 'p':
	    idf_norm = IDF_NORM::PROB;
	    break;
	default:
	    idf_norm = IDF_NORM::TFIDF;
    }
    wt_norm = WT_NORM::NONE;
}

TfIdfWeight::TfIdfWeight(WDF_NORM wdf_norm_,
			 IDF_NORM idf_norm_,
			 WT_NORM wt_norm_)
    : TfIdfWeight::TfIdfWeight(wdf_norm_, idf_norm_, wt_norm_, 0.2, 1.0) {}

TfIdfWeight::TfIdfWeight(WDF_NORM wdf_norm_, IDF_NORM idf_norm_,
			 WT_NORM wt_norm_, double slope, double delta)
    : wdf_norm(wdf_norm_), idf_norm(idf_norm_), wt_norm(wt_norm_),
      param_slope(slope), param_delta(delta)
{
    if (param_slope <= 0)
	throw Xapian::InvalidArgumentError("Parameter slope is invalid");
    if (param_delta <= 0)
	throw Xapian::InvalidArgumentError("Parameter delta is invalid");
    if (idf_norm != IDF_NORM::NONE) {
	need_stat(TERMFREQ);
	need_stat(COLLECTION_SIZE);
    }
    need_stat(WDF);
    need_stat(WDF_MAX);
    need_stat(WQF);
    if (wdf_norm == WDF_NORM::PIVOTED || idf_norm == IDF_NORM::PIVOTED) {
	need_stat(AVERAGE_LENGTH);
	need_stat(DOC_LENGTH);
	need_stat(DOC_LENGTH_MIN);
    }
    if (wdf_norm == WDF_NORM::LOG_AVERAGE) {
	need_stat(DOC_LENGTH);
	need_stat(DOC_LENGTH_MIN);
	need_stat(DOC_LENGTH_MAX);
	need_stat(UNIQUE_TERMS);
    }
}

TfIdfWeight *
TfIdfWeight::clone() const
{
    return new TfIdfWeight(wdf_norm, idf_norm, wt_norm,
			   param_slope, param_delta);
}

void
TfIdfWeight::init(double factor_)
{
    if (factor_ == 0.0) {
	// This object is for the term-independent contribution, and that's
	// always zero for this scheme.
	return;
    }

    wqf_factor = get_wqf() * factor_;
    idfn = get_idfn(idf_norm);
}

string
TfIdfWeight::name() const
{
    return "Xapian::TfIdfWeight";
}

string
TfIdfWeight::short_name() const
{
    return "tfidf";
}

string
TfIdfWeight::serialise() const
{
    string result = serialise_double(param_slope);
    result += serialise_double(param_delta);
    result += static_cast<unsigned char>(wdf_norm);
    result += static_cast<unsigned char>(idf_norm);
    result += static_cast<unsigned char>(wt_norm);
    return result;
}

TfIdfWeight *
TfIdfWeight::unserialise(const string & s) const
{
    const char *ptr = s.data();
    const char *end = ptr + s.size();
    double slope = unserialise_double(&ptr, end);
    double delta = unserialise_double(&ptr, end);
    WDF_NORM wdf_norm_ = static_cast<WDF_NORM>(*(ptr)++);
    IDF_NORM idf_norm_ = static_cast<IDF_NORM>(*(ptr)++);
    WT_NORM wt_norm_ = static_cast<WT_NORM>(*(ptr)++);
    if (rare(ptr != end))
	throw Xapian::SerialisationError("Extra data in TfIdfWeight::unserialise()");
    return new TfIdfWeight(wdf_norm_, idf_norm_, wt_norm_, slope, delta);
}

double
TfIdfWeight::get_sumpart(Xapian::termcount wdf, Xapian::termcount doclen,
			 Xapian::termcount uniqterms) const
{
    double wdfn = get_wdfn(wdf, doclen, uniqterms, wdf_norm);
    return get_wtn(wdfn * idfn, wt_norm) * wqf_factor;
}

// An upper bound can be calculated simply on the basis of wdf_max as termfreq
// and N are constants.
double
TfIdfWeight::get_maxpart() const
{
    Xapian::termcount wdf_max = get_wdf_upper_bound();
    Xapian::termcount len_min = get_doclength_lower_bound();
    double wdfn = get_wdfn(wdf_max, len_min, len_min, wdf_norm);
    return get_wtn(wdfn * idfn, wt_norm) * wqf_factor;
}

// There is no extra per document component in the TfIdfWeighting scheme.
double
TfIdfWeight::get_sumextra(Xapian::termcount, Xapian::termcount) const
{
    return 0;
}

double
TfIdfWeight::get_maxextra() const
{
    return 0;
}

// Return normalized wdf, idf and weight depending on the normalization string.
double
TfIdfWeight::get_wdfn(Xapian::termcount wdf, Xapian::termcount doclen,
		      Xapian::termcount uniqterms, WDF_NORM wdf_norm_) const
{
    switch (wdf_norm_) {
	case WDF_NORM::BOOLEAN:
	    if (wdf == 0) return 0;
	    return 1.0;
	case WDF_NORM::SQUARE:
	    return (wdf * wdf);
	case WDF_NORM::LOG:
	    if (wdf == 0) return 0;
	    return (1 + log(double(wdf)));
	case WDF_NORM::PIVOTED: {
	    if (wdf == 0) return 0;
	    double normlen = doclen / get_average_length();
	    double norm_factor = 1 / (1 - param_slope + (param_slope * normlen));
	    return ((1 + log(1 + log(double(wdf)))) * norm_factor + param_delta);
	}
	case WDF_NORM::LOG_AVERAGE: {
	    if (wdf == 0) return 0;
	    double uniqterm_double = uniqterms;
	    double doclen_double = doclen;
	    double wdf_avg = 1;
	    if (doclen_double == 0 || uniqterm_double == 0)
		wdf_avg = 1;
	    else
		wdf_avg = doclen_double / uniqterm_double;
	    double num = 1 + log(double(wdf));
	    double den = 1 + log(wdf_avg);
	    return num / den;
	}
	default:
	    return wdf;
    }
}

double
TfIdfWeight::get_idfn(IDF_NORM idf_norm_) const
{
    Xapian::doccount termfreq = 1;
    if (idf_norm_ != IDF_NORM::NONE) termfreq = get_termfreq();
    double N = 1.0;
    if ((idf_norm_ == IDF_NORM::PROB) || (idf_norm_ == IDF_NORM::SQUARE) ||
	(idf_norm_ == IDF_NORM::TFIDF) || (idf_norm_ == IDF_NORM::PIVOTED))
	N = get_collection_size();
    switch (idf_norm_) {
	case IDF_NORM::NONE:
	    return 1.0;
	case IDF_NORM::PROB:
	    // All documents are indexed by the term
	    if (N == termfreq) return 0;
	    return log((N - termfreq) / termfreq);
	case IDF_NORM::FREQ:
	    return (1.0 / termfreq);
	case IDF_NORM::SQUARE:
	    return pow(log(N / termfreq), 2.0);
	case IDF_NORM::PIVOTED:
	    return log((N + 1) / termfreq);
	default:
	    return (log(N / termfreq));
    }
}

double
TfIdfWeight::get_wtn(double wt, WT_NORM wt_norm_) const
{
    (void)wt_norm_;
    return wt;
}

TfIdfWeight *
TfIdfWeight::create_from_parameters(const char * p) const
{
    if (*p == '\0')
	return new Xapian::TfIdfWeight();
    return new Xapian::TfIdfWeight(p);
}

}
