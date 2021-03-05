#include <objmgr/object_manager.hpp>
#include <objmgr/scope.hpp>
#include <objmgr/util/create_defline.hpp>
#include <objtools/blast/blastdb_format/blastdb_dataextract.hpp>
#include <corelib/ncbiutil.hpp>
#include "blastdb.h"

using std::cout;
using std::endl;
using std::vector;
using namespace ncbi;

static string full_id(CBioseq& bioseq, CBioseq_Handle* bioseq_handle, bool long_ids) {
	string id;
	if (long_ids) {
		CConstRef<CSeq_id> best_id = FindBestChoice(bioseq.GetId(), CSeq_id::FastaAARank);
		id = best_id->AsFastaString();
		sequence::CDeflineGenerator gen;
		id += gen.GenerateDefline(*bioseq_handle, 0);
	}
	else {
		CBlastDeflineUtil::ProcessFastaDeflines(bioseq, id, false);
		id.erase(0, 1);
		id.pop_back();
	}
	return id;
}

template<typename _it>
static void load_seq_data(CBioseq& bioseq, CBioseq_Handle bioseq_handle, _it it) {
	ncbi::objects::CSeqVector v = bioseq_handle.GetSeqVector(CBioseq_Handle::eCoding_Iupac);
	if (v.GetCoding() != CSeq_data::e_Iupacaa)
		throw std::runtime_error("Invalid sequence coding in BLAST database.");
	
	for (size_t i = 0; i < v.size(); ++i) {
		const auto l = v[i] & 31;
		const Letter s = IUPACAA_TO_STD[l];
		if (s == -1)
			throw std::runtime_error("Unrecognized sequence character in BLAST database letter=" + std::to_string(l)
				+ " accession=" + bioseq.GetFirstId()->AsFastaString()
				+ " position=" + std::to_string(i + 1));
		*it = s;
		++it;
	}
}

BlastDB::BlastDB(const std::string& file_name, Flags flags) :
	SequenceFile(Type::BLAST),
	db_(file_name, ncbi::CSeqDB::eProtein),
	oid_(0),
	oid_seqdata_(0),
	long_seqids_(false),
	flags_(flags)
{
}

void BlastDB::init_seqinfo_access()
{
}

void BlastDB::init_seq_access()
{
}

void BlastDB::seek_chunk(const Chunk& chunk)
{
}

size_t BlastDB::tell_seq() const
{
	return oid_;
}

SeqInfo BlastDB::read_seqinfo()
{
	if (oid_ >= db_.GetNumSeqs()) {
		++oid_;
		return SeqInfo(0, 0);
	}
	const char* buf;
	const int l = db_.GetSequence(oid_, &buf);
	db_.RetSequence(&buf);
	if (l == 0)
		throw std::runtime_error("Database with sequence length 0 is not supported");
	return SeqInfo(oid_++, l);
}

void BlastDB::putback_seqinfo()
{
	--oid_;
}

size_t BlastDB::id_len(const SeqInfo& seq_info, const SeqInfo& seq_info_next)
{
	if(flag_get(flags_, Flags::FULL_SEQIDS))
		return full_id(*db_.GetBioseq(seq_info.pos), nullptr, long_seqids_).length();
	else {
		list<CRef<CSeq_id>> ids = db_.GetSeqIDs(oid_seqdata_);
		return ids.front()->GetSeqIdString().length();
	}
}

void BlastDB::seek_offset(size_t p)
{
	oid_seqdata_ = (int)p;
}

void BlastDB::read_seq_data(Letter* dst, size_t len)
{
	*(dst - 1) = Sequence::DELIMITER;
	*(dst + len) = Sequence::DELIMITER;
	const char* buf;
	const int db_len = db_.GetSequence(oid_seqdata_, &buf);
	if (size_t(db_len) != len)
		throw std::runtime_error("Incorrect length");
	
	for (int i = 0; i < db_len; ++i) {
		const Letter l = (int)buf[i];
		if (l >= sizeof(NCBI_TO_STD) || NCBI_TO_STD[l] == -1) {
			list<CRef<CSeq_id>> ids = db_.GetSeqIDs(oid_seqdata_);
			throw std::runtime_error("Unrecognized sequence character in BLAST database ("
				+ std::to_string(l)
				+ ", id=" + ids.front()->GetSeqIdString()
				+ ", pos=" + std::to_string(i) + ')');
		}
		*(dst++) = NCBI_TO_STD[l];
	}
	db_.RetSequence(&buf);
}

void BlastDB::read_id_data(char* dst, size_t len)
{
	if (flag_get(flags_, Flags::FULL_SEQIDS)) {
		const string id = full_id(*db_.GetBioseq(oid_seqdata_), nullptr, long_seqids_);
		std::copy(id.begin(), id.begin() + len, dst);
	}
	else {
		list<CRef<CSeq_id>> ids = db_.GetSeqIDs(oid_seqdata_);
		const string id = ids.front()->GetSeqIdString();
		ids.front()->FastaAAScore();
		std::copy(id.begin(), id.end(), dst);
	}
	dst[len] = '\0';
	++oid_seqdata_;
}

void BlastDB::skip_id_data()
{
	++oid_seqdata_;
}

size_t BlastDB::sequence_count() const
{
	return db_.GetNumSeqs();
}

size_t BlastDB::letters() const
{
	return db_.GetTotalLength();
}

int BlastDB::db_version() const
{
	return (int)db_.GetBlastDbVersion();
}

int BlastDB::program_build_version() const
{
	return 0;
}

void BlastDB::read_seq(std::vector<Letter>& seq, std::string& id)
{
	id.clear();
	CRef<CBioseq> bioseq = db_.GetBioseq(oid_);
	CScope scope(*CObjectManager::GetInstance());
	CBioseq_Handle bioseq_handle = scope.AddBioseq(*bioseq);

	id = full_id(*bioseq, &bioseq_handle, long_seqids_);

	seq.clear();
	load_seq_data(*bioseq, bioseq_handle, std::back_inserter(seq));
	
	++oid_;
}

void BlastDB::check_metadata(int flags) const
{
	if ((flags & TAXON_NODES) || (flags & TAXON_MAPPING) || (flags & TAXON_SCIENTIFIC_NAMES))
		throw std::runtime_error("Taxonomy features are not supported for the BLAST database format.");
}

int BlastDB::metadata() const
{
	return 0;
}

TaxonList* BlastDB::taxon_list()
{
	return nullptr;
}

TaxonomyNodes* BlastDB::taxon_nodes()
{
	return nullptr;
}

std::vector<string>* BlastDB::taxon_scientific_names()
{
	return nullptr;
}

int BlastDB::build_version()
{
	return 0;
}

void BlastDB::create_partition_balanced(size_t max_letters)
{
}

void BlastDB::save_partition(const std::string& partition_file_name, const std::string& annotation)
{
}

size_t BlastDB::get_n_partition_chunks()
{
	return size_t();
}

void BlastDB::set_seqinfo_ptr(size_t i)
{
	oid_ = (int)i;
}

void BlastDB::close()
{
}

BlastDB::~BlastDB()
{
}