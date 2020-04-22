#include <iostream>
#include <TFile.h>
#include <TTreeReader.h>
#include <TTreeReaderArray.h>
//#include <include/cxxopts.hpp>
#include <util/Util.h>
#include <util/IntMatrix.h>
#include <util/ChipIdentifier.h>
#include <util/Serializer.h>
#include <encode/interface/Encoder.h>
#include <encode/interface/QCore.h>

using namespace std;



int main (int argc, char* argv[])
{

    if (argc == 1)
        std::cout << "please call like so: bin/makestream filepath nevents" << std::endl;

    if (argc != 3)
        std::cout << "please call like so: bin/makestream filepath nevents" << std::endl;

    std::string filename = argv[1];

    TFile* file = new TFile (filename.c_str() );
    TTreeReader reader ("BRIL_IT_Analysis/Digis", file);
    TTreeReaderArray<bool> trv_barrel (reader, "barrel");
    TTreeReaderArray<uint32_t> trv_module (reader, "module");
    TTreeReaderArray<uint32_t> trv_adc (reader, "adc");
    TTreeReaderArray<uint32_t> trv_col (reader, "column");
    TTreeReaderArray<uint32_t> trv_row (reader, "row");
    TTreeReaderArray<uint32_t> trv_ringlayer (reader, "ringlayer");
    TTreeReaderArray<uint32_t> trv_diskladder (reader, "diskladder");

    uint32_t nrows_module = 672;
    uint32_t ncols_module = 864;
    int nevents = 0;

    uint32_t nevent = 0;

    Serializer ser; //an instance of my Serialzier class that does not need to be a class but it is convenient



    // Event loop
    while (reader.Next() )
    {
        if (nevent > nevents)
            break;

        uint32_t ientry = 0;

        // Read all data for this event and construct
        // 2D matrices of pixel ADCs, key == chip identifier
        std::map<ChipIdentifier, IntMatrix> module_matrices;
        std::map<ChipIdentifier, IntMatrix> chip_matrices;
        // QCore objects per module, key == chip identifier
        std::map<ChipIdentifier, std::vector<QCore>> qcores;
        //let's use a seperate instance of the encoder for each event, just to be sure
        Encoder enc;

        // module loop witin event
        // this has one entry for each hit (row, col, adc) touple
        for ( auto imod : trv_module)
        {
            bool barrel = trv_barrel.At (ientry);
            uint32_t diskladder = trv_diskladder.At (ientry);
            uint32_t ringlayer = trv_ringlayer.At (ientry);
            uint32_t module = imod;

            // if it's barrel or not barrel but disk < 9 (TFPX) increment ientry (consider next digi) and continue so only consider TEPX
            if (barrel || (!barrel && diskladder < 9) )
            {
                ientry++;
                continue;
            }

            //generate a temporary chip identifier with fake chip id 99 since for the moment we just care for the module
            ChipIdentifier tmp_id (diskladder, ringlayer, module, 99);
            //check if that module is already in our map
            auto matrices_iterator = module_matrices.find (tmp_id);

            if (matrices_iterator == std::end (module_matrices) ) // not found
            {
                //insert an empty IntMatrix
                IntMatrix tmp_matrix (nrows_module, ncols_module);
                //module_matrices[tmp_id] = tmp_matrix;
                module_matrices.emplace (tmp_id, tmp_matrix);
            }

            //in all other cases the Intmatrix for that module exists already
            //get the row, col and ADC (attention, sensor row and column address) for the current module
            uint32_t row = trv_row.At (ientry);
            uint32_t col = trv_col.At (ientry);
            uint32_t adc = trv_adc.At (ientry);
            //some sanity checks
            //convert to module address
            module_matrices.at (tmp_id).convertPitch_andFill (row, col, adc);
            ientry++;
        }//end module loop

        // Loop over modules and create qcores for each module
        std::cout << "Finished reading full data for Event " << nevent << " from the tree; found " << module_matrices.size() << " modules for TEPX" << std::endl;

        //now split the tmp_matrix in 4 chip matrices, construct the Chip identifier object and insert into the matrices map
        for (auto matrix : module_matrices)
        {
            for (uint32_t chip = 0; chip < 4; chip++)
            {
                ChipIdentifier chipId (matrix.first.mdisk, matrix.first.mring, matrix.first.mmodule, chip);
                IntMatrix tmp_matrix = matrix.second.submatrix (chip);
                chip_matrices.emplace (chipId, tmp_matrix);
            }
        }

        std::cout << "Finished picking apart modules for Event " << nevent << " ; converted " << module_matrices.size() << " modules for TEPX to " << chip_matrices.size() << " chips" << std::endl;




        //int nqcore = 0;
        for (auto chip : chip_matrices )
        {
            //Print out chip coordinates
            //chip.first.print();
            //pick apart each chip int matrix in QCores
            std::vector<QCore> qcores = enc.qcores (chip.second, nevent, chip.first.mmodule, chip.first.mchip);

            //create the actual stream for this chip
            std::stringstream ss;
            std::vector<bool> tmp =  ser.serializeChip (qcores, nevent, chip.first.mchip, true, true, ss);

            std::vector<uint16_t> binary_vec = ser.toVec<uint16_t> (tmp);
            std::cout << "Size in 64 bit words: " <<  tmp.size() / 64 << " in 16 bit words " << tmp.size() / 16 << " of the resulting binary vec (remember: +1 for the number of data words!) " << binary_vec.size() << std::endl;

            //chip.first.print();
            //std::cout << "Stream size in 64-bit words for this chip: " << tmp.size() / 64 << std::endl;
            //std::cout << ss.str() << std::endl;
        }

        nevent++;
    }//end event loop
}
