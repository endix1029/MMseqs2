#include "DBReader.h"
#include "CommandCaller.h"
#include "Util.h"
#include "FileUtil.h"
#include "Debug.h"
#include "PrefilteringIndexReader.h"
#include "linsearch.sh.h"

namespace Linsearch {
#include "translated_search.sh.h"
}
#include <iomanip>
#include <climits>
#include <cassert>
#include <LinsearchIndexReader.h>


void setLinsearchDefaults(Parameters *p) {
    p->spacedKmer = true;
    p->alignmentMode = Parameters::ALIGNMENT_MODE_SCORE_COV;
    p->sensitivity = 5.7;
    p->evalThr = 0.001;
    //p->orfLongest = true;
    p->orfStartMode = 1;
    p->orfMinLength = 30;
    p->orfMaxLength = 32734;
    p->evalProfile = 0.1;
}


int linsearch(int argc, const char **argv, const Command& command) {
    Parameters &par = Parameters::getInstance();
    setLinsearchDefaults(&par);
    par.overrideParameterDescription((Command &) command, par.PARAM_COV_MODE.uniqid, NULL, NULL,
                                     par.PARAM_COV_MODE.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &) command, par.PARAM_C.uniqid, NULL, NULL,
                                     par.PARAM_C.category | MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &) command, par.PARAM_MIN_SEQ_ID.uniqid, NULL, NULL,
                                     par.PARAM_MIN_SEQ_ID.category | MMseqsParameter::COMMAND_EXPERT);
    for (size_t i = 0; i < par.extractorfs.size(); i++) {
        par.overrideParameterDescription((Command &) command, par.extractorfs[i]->uniqid, NULL, NULL,
                                         par.extractorfs[i]->category | MMseqsParameter::COMMAND_EXPERT);
    }
    for (size_t i = 0; i < par.translatenucs.size(); i++) {
        par.overrideParameterDescription((Command &) command, par.translatenucs[i]->uniqid, NULL, NULL,
                                         par.translatenucs[i]->category | MMseqsParameter::COMMAND_EXPERT);
    }
    par.overrideParameterDescription((Command &) command, par.PARAM_THREADS.uniqid, NULL, NULL,
                                     par.PARAM_THREADS.category & ~MMseqsParameter::COMMAND_EXPERT);
    par.overrideParameterDescription((Command &) command, par.PARAM_V.uniqid, NULL, NULL,
                                     par.PARAM_V.category & ~MMseqsParameter::COMMAND_EXPERT);
    par.parseParameters(argc, argv, command, 4, false, 0,
                        MMseqsParameter::COMMAND_ALIGN | MMseqsParameter::COMMAND_PREFILTER);


    const int queryDbType = DBReader<unsigned int>::parseDbType(par.db1.c_str());
    const int targetDbType = DBReader<unsigned int>::parseDbType(par.db2.c_str());
    if (queryDbType == -1 || targetDbType == -1) {
        Debug(Debug::ERROR)
                << "Please recreate your database or add a .dbtype file to your sequence/profile database.\n";
        EXIT(EXIT_FAILURE);
    }

    if (Parameters::isEqualDbtype(queryDbType, Parameters::DBTYPE_HMM_PROFILE) &&
            Parameters::isEqualDbtype(targetDbType,Parameters::DBTYPE_HMM_PROFILE)) {
        Debug(Debug::ERROR) << "Profile-Profile searches are not supported.\n";
        EXIT(EXIT_FAILURE);
    }

    const bool isNuclSearch = (Parameters::isEqualDbtype(queryDbType, Parameters::DBTYPE_NUCLEOTIDES)
                            && Parameters::isEqualDbtype(targetDbType, Parameters::DBTYPE_NUCLEOTIDES));
//    if(isNuclSearch == true){
//        setNuclSearchDefaults(&par);
//    }else{
//        par.overrideParameterDescription((Command &) command, par.PARAM_STRAND.uniqid, NULL, NULL,
//                                         par.PARAM_STRAND.category | MMseqsParameter::COMMAND_EXPERT);
//    }

    std::string indexStr = LinsearchIndexReader::searchForIndex(par.db2);
    if(indexStr.size() == 0){
        Debug(Debug::ERROR) << par.db2  << " needs to be index.\n";
        Debug(Debug::ERROR) << "createlinindex " << par.db2 << ".\n";
        EXIT(EXIT_FAILURE);
    }
    par.filenames[1] = indexStr;
    const bool isTranslatedNuclSearch =
            isNuclSearch==false && (Parameters::isEqualDbtype(queryDbType, Parameters::DBTYPE_NUCLEOTIDES) ||
                                    Parameters::isEqualDbtype(targetDbType, Parameters::DBTYPE_NUCLEOTIDES));

    const bool isUngappedMode = par.alignmentMode == Parameters::ALIGNMENT_MODE_UNGAPPED;
    if (isUngappedMode && (Parameters::isEqualDbtype(queryDbType, Parameters::DBTYPE_HMM_PROFILE) || Parameters::isEqualDbtype(targetDbType,Parameters::DBTYPE_HMM_PROFILE))) {
        par.printUsageMessage(command, MMseqsParameter::COMMAND_ALIGN | MMseqsParameter::COMMAND_PREFILTER);
        Debug(Debug::ERROR) << "Cannot use ungapped alignment mode with profile databases.\n";
        EXIT(EXIT_FAILURE);
    }

    par.printParameters(command.cmd, argc, argv, par.searchworkflow);
    if (FileUtil::directoryExists(par.db4.c_str()) == false) {
        Debug(Debug::INFO) << "Tmp " << par.db4 << " folder does not exist or is not a directory.\n";
        if (FileUtil::makeDir(par.db4.c_str()) == false) {
            Debug(Debug::ERROR) << "Could not create tmp folder " << par.db4 << ".\n";
            EXIT(EXIT_FAILURE);
        } else {
            Debug(Debug::INFO) << "Created dir " << par.db4 << "\n";
        }
    }

    std::string hash = SSTR(par.hashParameter(par.filenames, par.searchworkflow));
    if(par.reuseLatest){
        hash = FileUtil::getHashFromSymLink(par.db4+"/latest");
    }
    std::string tmpDir = par.db4+"/"+hash;
    if (FileUtil::directoryExists(tmpDir.c_str()) == false) {
        if (FileUtil::makeDir(tmpDir.c_str()) == false) {
            Debug(Debug::ERROR) << "Could not create sub tmp folder " << tmpDir << ".\n";
            EXIT(EXIT_FAILURE);
        }
    }
    par.filenames.pop_back();
    par.filenames.push_back(tmpDir);
    FileUtil::symlinkAlias(tmpDir, "latest");
    CommandCaller cmd;

    std::string program = tmpDir + "/linsearch.sh";
    cmd.addVariable("ALIGN_MODULE", isUngappedMode ? "rescorediagonal" : "align");
    cmd.addVariable("KMERSEARCH_PAR", par.createParameterString(par.kmersearch).c_str());
    cmd.addVariable("ALIGNMENT_PAR", par.createParameterString(par.align).c_str());
    cmd.addVariable("SWAPRESULT_PAR", par.createParameterString(par.swapresult).c_str());
    if(isNuclSearch){
        cmd.addVariable("NUCL", "1");
    }
    FileUtil::writeFile(program, linsearch_sh, linsearch_sh_len);

    if (isTranslatedNuclSearch == true) {
        cmd.addVariable("NO_TARGET_INDEX", (indexStr == "") ? "TRUE" : NULL);
        FileUtil::writeFile(tmpDir + "/translated_search.sh", Linsearch::translated_search_sh, Linsearch::translated_search_sh_len);
        cmd.addVariable("QUERY_NUCL", Parameters::isEqualDbtype(queryDbType,Parameters::DBTYPE_NUCLEOTIDES) ? "TRUE" : NULL);
        cmd.addVariable("TARGET_NUCL", Parameters::isEqualDbtype(targetDbType,Parameters::DBTYPE_NUCLEOTIDES) ? "TRUE" : NULL);
        cmd.addVariable("ORF_PAR", par.createParameterString(par.extractorfs).c_str());
        cmd.addVariable("OFFSETALIGNMENT_PAR", par.createParameterString(par.onlythreads).c_str());
        cmd.addVariable("TRANSLATE_PAR", par.createParameterString(par.translatenucs).c_str());
        cmd.addVariable("SEARCH", program.c_str());
        program = std::string(tmpDir + "/translated_search.sh");
    }
    cmd.execProgram(program.c_str(), par.filenames);

    // Should never get here
    assert(false);

    return 0;
}
