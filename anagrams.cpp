#include <bits/stdc++.h>
using namespace std;

#define rep(i, from, to) for (int i = from; i < (to); ++i)
#define trav(a, x) for (auto& a : x)
#define all(x) x.begin(), x.end()
#define sz(x) (int)(x).size()
typedef long long ll;
typedef pair<int, int> pii;
typedef vector<int> vi;

constexpr int ALPHA = 32;
constexpr char NOCHAR = (char)-1;
constexpr int FORMAT_VERSION = 0x10010001;
typedef array<short, ALPHA> FreqCount;

#define ALIGN4(x) (((x) + 3) & -4)

struct Word {
	uint32_t index;
};
bool operator<(Word a, Word b) {
	return a.index < b.index;
}

struct Arena;

template<class T>
struct SmallPtr {
	uint32_t offset;
	SmallPtr(uint32_t offset) : offset(offset) {}
	T* get(Arena& base);
	const T* get(Arena& base) const;
	static SmallPtr wild() { return SmallPtr((uint32_t) -1); }
};

struct Arena {
	vector<char> mem;
	Arena() {}
	Arena(vector<char>&& mem) : mem(move(mem)) {}
	uint32_t size() const { return (uint32_t) mem.size(); }
	char* base() { return mem.data(); }
	const char* base() const { return mem.data(); }
	uint32_t rawAlloc(size_t size) {
		size = ALIGN4(size);
		uint32_t ret = this->size();
		size_t newSize = size + ret;
		assert(newSize <= (1U << 30) - 1 + (1U << 30));
		if (newSize > mem.capacity()) {
			mem.resize(mem.capacity() + newSize);
		}
		mem.resize(newSize);
		return (uint32_t) ret;
	}
	template<class T>
	SmallPtr<T> alloc() {
		return SmallPtr<T>(rawAlloc(sizeof(T)));
	}
	template<class T>
	SmallPtr<T> allocArray(size_t size) {
		return SmallPtr<T>(rawAlloc(sizeof(T) * size));
	}
};

template<class T>
struct StackPtr {
	const Arena* arena;
	SmallPtr<T> inner;
	StackPtr(const Arena& arena, SmallPtr<T> inner) : arena(&arena), inner(inner) {}
	T* operator->() { return inner.get((Arena&)*arena); }
	T& operator[](size_t i) { return this->operator->()[i]; }
	T& operator*() { return (*this)[0]; }

	const T* operator->() const { return inner.get(*arena); }
	const T& operator[](size_t i) const { return this->operator->()[i]; }
	const T& operator*() const { return (*this)[0]; }
};

template<class T>
T* SmallPtr<T>::get(Arena& base) {
	return (T*)(base.base() + offset);
}

template<class T>
const T* SmallPtr<T>::get(Arena& base) const {
	return (T*)(base.base() + offset);
}

struct Trie {
	// If subCount == LEAF, there are no strings in this part of the trie.
	// If subCount & LEAF but (subCount & ~LEAF) != 0, subs represents a list
	// of Word's, and 'leaf()' is a NOCHAR-terminated string with the characters
	// that make up the words in that list, sorted.
	// If !(subCount & LEAF), subs is a list of Trie's.
	constexpr static int LEAF = 1 << 30;
	int subCount;
	SmallPtr<uint32_t> subs;
	bool isEmpty() const { return subCount == LEAF; }
	bool isLeaf() const { return subCount & LEAF; }
	int count() const { return subCount & ~LEAF; }
	const char* leaf() const {
		return (const char*) this + ALIGN4(sizeof(Trie));
	}
};

// 70 MB, && occ[i] > 0 -> 68 MB, alphabetical -> 56 MB, max -> 47 MB, ideal ~ 15 MB
struct DS {
	Arena arena;
	SmallPtr<Trie> trie;

	// A chunk of null-terminated UTF-8 strings, pointed into by Word.index.
	SmallPtr<char> wordlist;

	using Callback = function<void(const vector<Word>&)>;
	void findAnagrams(const string& word, int numWords, Callback cb) const;

	void _findAnagrams(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb) const;

	void _rec(vector<StackPtr<Trie>>& iters, vector<Word>& sentence, Callback cb) const;

	void _rec2(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, Callback cb, size_t at2, int freq, int maxFreq) const;
};

string internalForm(const string& input) {
	string out;
	for (size_t i = 0; i < input.size(); i++) {
		char c = input[i];
		char e = NOCHAR;
		if ('a' <= c && c <= 'z')
			e = (char)(c - 'a');
		else if (c == (char)0xc3 && i + 1 != input.size()) {
			i++;
			char d = input[i];
			if (d == '\xa5') e = 26;
			else if (d == '\xa4') e = 27;
			else if (d == '\xb6') e = 28;
		}
		else if (c == '1')
			e = 29;
		else if (c == '3')
			e = 30;
		else if (c == '4')
			e = 31;
		if (e == NOCHAR) {
			cerr << "bad word: " << input << endl;
			exit(1);
		}
		out.push_back(e);
	}
	sort(all(out));
	return out;
}

string externalForm(const string& input) {
	string out;
	for (size_t i = 0; i < input.size(); i++) {
		char c = input[i], d;
		assert(c >= 0 && c != NOCHAR);
		if (c < 26) d = (char)('a' + c);
		else if (c < 29) {
			out += '\xc3';
			if (c == 26) d = '\xa5';
			else if (c == 27) d = '\xa4';
			else d = '\xb6';
		}
		else if (c == 29) d = '1';
		else if (c == 30) d = '3';
		else if (c == 31) d = '4';
		else {
			cerr << "Bad character " << (int) c << endl;
			exit(1);
		}
		out += d;
	}
	return out;
}

SmallPtr<Trie> buildTrie(Arena& arena, vector<pair<string, vector<Word>>>& words, int which) {
	if (words.size() == 0) {
		StackPtr<Trie> ret{arena, arena.alloc<Trie>()};
		ret->subCount = Trie::LEAF;
		ret->subs = SmallPtr<uint32_t>::wild();
		return ret.inner;
	} else if (words.size() == 1) {
		StackPtr<Trie> ret{arena, arena.alloc<Trie>()};
		string& s = words[0].first;
		auto& ws = words[0].second;
		StackPtr<char> after{arena, arena.allocArray<char>(s.size() + 1)};
		memcpy(&after[0], s.c_str(), s.size());
		after[s.size()] = NOCHAR;
		int subCount = (int) ws.size();
		ret->subCount = subCount | Trie::LEAF;
		StackPtr<uint32_t> leafWords{arena, arena.allocArray<uint32_t>(ws.size())};
		ret->subs = leafWords.inner;
		for (int i = 0; i < subCount; i++) {
			leafWords[i] = ws[i].index;
		}
		return ret.inner;
	} else {
		assert(which != ALPHA);
		StackPtr<Trie> ret{arena, arena.alloc<Trie>()};
		vector<vector<pair<string, vector<Word>>>> subwords;
		for (auto& pa : words) {
			int freq = 0;
			for (char c : pa.first) {
				if (c == which) freq++;
			}
			while (freq >= sz(subwords)) {
				subwords.emplace_back();
			}
			subwords[freq].push_back(move(pa));
		}
		words.clear();
		words.shrink_to_fit();

		int subCount = (int) subwords.size();
		ret->subCount = subCount;
		assert(subCount != 0);
		StackPtr<uint32_t> subs{arena, arena.allocArray<uint32_t>(subwords.size())};
		ret->subs = subs.inner;

		for (int i = 0; i < subCount; i++) {
			auto& sub = subwords[i];
			SmallPtr<Trie> subTrie = buildTrie(arena, sub, which + 1);
			subs[i] = subTrie.offset;
		}
		return ret.inner;
	}
}

void setFreqCount(StackPtr<Trie> t, FreqCount& freq) {
	assert(t->isLeaf());
	memset(freq.data(), 0, sizeof(freq));
	const char* c = t->leaf();
	while (*c != NOCHAR) {
		freq[*c]++;
		c++;
	}
}

DS buildDS(string filename) {
	Arena arena;
	string wordlist;
	ifstream fin(filename);
	assert(fin);
	string line;
	map<string, vector<Word>> lookup;
	while (getline(fin, line)) {
		size_t ind = line.find(' ');
		string word = internalForm(line.substr(0, ind));
		if (ind != string::npos) {
			line = line.substr(ind + 1);
		}
		Word w{(uint32_t) wordlist.size()};
		wordlist += line;
		wordlist += '\0';
		lookup[word].push_back(w);
	}
	vector<pair<string, vector<Word>>> words(all(lookup));
	SmallPtr<Trie> trie = buildTrie(arena, words, 0);
	StackPtr<char> arenaWl{arena, arena.allocArray<char>(wordlist.size())};
	memcpy(&*arenaWl, wordlist.data(), wordlist.size());
	return {move(arena), trie, arenaWl.inner};
}

void DS::findAnagrams(const string& word, int numWords, DS::Callback cb) const {
	string w = internalForm(word);
	FreqCount wFreqCount{};
	for (int c : w) {
		assert(0 <= c && c < ALPHA);
		wFreqCount[c]++;
	}
	vector<FreqCount> leafFreqCounts(numWords);
	StackPtr<Trie> tr{this->arena, this->trie};
	assert(!tr->isLeaf());
	vector<StackPtr<Trie>> iters(numWords, tr);
	_findAnagrams(wFreqCount, 0, iters, leafFreqCounts.data(), cb);
}

void DS::_rec(vector<StackPtr<Trie>>& iters, vector<Word>& sentence, DS::Callback cb) const {
	if (sentence.size() == iters.size()) {
		cb(sentence);
	} else {
		StackPtr<Trie> t = iters[sentence.size()];
		assert(t->isLeaf());
		int sc = t->count();
		assert(sc != 0);
		StackPtr<uint32_t> subs{this->arena, t->subs};
		for (int i = 0; i < sc; i++) {
			sentence.push_back(Word{subs[i]});
			_rec(iters, sentence, cb);
			sentence.pop_back();
		}
	}
}

void DS::_rec2(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb, size_t at2, int freq, int maxFreq) const {
	// freq: remaining frequency to distribute
	// maxFreq: how much more frequency we can distribute over the rest of the iterators
	// (both counts ignore leaf nodes)
	if (freq > maxFreq) return;
	if (at2 == iters.size()) {
		assert(maxFreq == 0);
		assert(freq == 0);
		this->_findAnagrams(wFreqCount, at, iters, leafFreqCounts, cb);
		return;
	}
	StackPtr<Trie> t = iters[at2];
	if (t->isLeaf()) {
		this->_rec2(wFreqCount, at, iters, leafFreqCounts, cb, at2 + 1, freq, maxFreq);
	} else {
		int mf = t->count() - 1;
		StackPtr<uint32_t> subs{this->arena, t->subs};
		for (int f = 0; f <= min(freq, mf); f++) {
			StackPtr<Trie> nt{this->arena, SmallPtr<Trie>(subs[f])};
			if (nt->isEmpty()) continue;
			if (nt->isLeaf()) {
				setFreqCount(nt, leafFreqCounts[at2]);
			}
			iters[at2] = nt;
			this->_rec2(wFreqCount, at, iters, leafFreqCounts, cb, at2 + 1, freq - f, maxFreq - mf);
		}
		iters[at2] = t;
	}
}

void DS::_findAnagrams(const FreqCount& wFreqCount, int at, vector<StackPtr<Trie>>& iters, FreqCount* leafFreqCounts, DS::Callback cb) const {
	if (at == ALPHA) {
		vector<Word> sentence;
		this->_rec(iters, sentence, cb);
		return;
	}
	assert(at != ALPHA);
	int freq = wFreqCount[at], maxFreq = 0;
	for (size_t i = 0; i < iters.size(); i++) {
		if (iters[i]->isLeaf()) {
			freq -= leafFreqCounts[i][at];
		} else {
			maxFreq += iters[i]->count() - 1;
		}
	}
	if (freq < 0 || freq > maxFreq) {
		return;
	}
	this->_rec2(wFreqCount, at + 1, iters, leafFreqCounts, cb, 0, freq, maxFreq);
}

void writeDS(DS& ds, const string& filename) {
	ofstream file(filename, ios::binary);
	file.exceptions(ios::failbit | ios::badbit | ios::eofbit);
	try {
		uint32_t header[4] = {
			FORMAT_VERSION,
			ds.arena.size(),
			ds.trie.offset,
			ds.wordlist.offset,
		};
		file.write((const char*) &header[0], sizeof(header));
		file.write(ds.arena.mem.data(), ds.arena.size());
	}
	catch (const ios_base::failure& exc) {
		cerr << "Failed to write " << filename << ": " << exc.what() << endl;
		exit(1);
	}
}

DS readDS(const string& filename) {
	// TODO: mmap
	ifstream file(filename, ios::binary);
	file.exceptions(ios::failbit | ios::badbit | ios::eofbit);
	try {
		uint32_t header[4];
		file.read((char*) &header[0], sizeof(header));
		uint32_t version = header[0];
		uint32_t arenaSize = header[1];
		uint32_t triePtr = header[2];
		uint32_t wordlistPtr = header[3];
		if (version != FORMAT_VERSION) {
			cerr << "Bad file format." << endl;
			exit(1);
		}
		vector<char> mem(arenaSize);
		file.read(mem.data(), arenaSize);
		return DS{Arena(move(mem)), SmallPtr<Trie>(triePtr), SmallPtr<char>(wordlistPtr)};
	}
	catch (const ios_base::failure& exc) {
		cerr << "Failed to read " << filename << ": " << exc.what() << endl;
		exit(1);
	}
}

void help(const char* program) {
	cout << "Usage:" << endl;
	cout << program << " --help | --build <dict.txt> | [--limit <N>] [--max-words <N>] [--file <file.dat>] [<word>...]" << endl;
	exit(0);
}

int main(int argc, char** argv) {
	int limit = 1000, maxWords = 1;
	string dsFilename = "dict.dat";
	string buildFilename;
	bool noMoreFlags = false;
	bool build = false;
	vector<string> nonflags;

	rep(i,1,argc) {
		string a = argv[i];
		auto next = [&]() -> string {
			if (i + 1 == argc) {
				cerr << "Missing argument parameter" << endl;
				exit(1);
			}
			i++;
			return argv[i];
		};
		if (!noMoreFlags && !a.empty() && a[0] == '-') {
			if (a == "--") {
				noMoreFlags = true;
			} else if (a == "--help") {
				help(argv[0]);
			} else if (a == "--build") {
				build = true;
				buildFilename = next();
			} else if (a == "--limit") {
				string x = next();
				limit = atoi(x.c_str());
			} else if (a == "--max-words") {
				string x = next();
				maxWords = atoi(x.c_str());
			} else if (a == "--file") {
				dsFilename = next();
			} else {
				cerr << "Unrecognized flag " << a << endl;
				return 1;
			}
		} else {
			nonflags.push_back(a);
		}
	}

	if (build) {
		DS ds = buildDS(buildFilename);
		cout << "ds size: " << ds.arena.size() << endl;
		writeDS(ds, dsFilename);
		exit(0);
	}

	DS ds = readDS(dsFilename);

	auto run = [&](const string& word) {
		int count = 0;
		try {
			for (int numWords = 1; count < limit && numWords <= maxWords; numWords++) {
				ds.findAnagrams(word, numWords, [&](const vector<Word>& sentence) -> void {
					if (!is_sorted(sentence.begin(), sentence.end()))
						return;
					StackPtr<char> wordlist{ds.arena, ds.wordlist};
					bool first = true;
					for (Word w : sentence) {
						if (first) first = false;
						else cout << ' ';
						const char* cw = &wordlist[w.index];
						cout << cw;
					}
					cout << endl;
					count++;
					if (count == limit)
						throw false;
				});
			}
		}
		catch (bool) {}
	};

	if (nonflags.size() != 0) {
		for (string& word : nonflags) {
			run(word);
		}
	} else {
		for (;;) {
			string word;
			cin >> word;
			run(word);
		}
	}
}
