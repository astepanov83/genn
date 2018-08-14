"""Generate SWIG interfaces
This module generates a number of SWIG interface (.i) files and Custom model
header and source files.

Generated interface files are:

    -- pygenn.i - interface of the main module
    -- newNeuronModels.i -- interface of the NeuronModels module
    -- newPostsynapticModels.i -- interface of the PostsynapticModels module
    -- newWeightUpdateModels.i -- interface of the WeightUpdateModels module
    -- currentSourceModels.i -- interface of the CurrentSourceModels module
    -- initVarSnippet.i -- interface of the InitVarSnippet module
    -- stl_containers.i -- interface of the StlContainers module which wraps
                           different specialization of std::vector and std::pair
    -- SharedLibraryModel.i -- interface of the SharedLibraryModel module which
                is used to load model at runtime

Generated headers and sources are:

    -- newNeuronModelsCustom.h/.cc -- header and source files for NeuronModels::Custom class
    -- newWeightUpdateModelsCustom.h/.cc -- header and source files for WeightUpdateModels::Custom class
    -- newPostsynapticModelsCustom.h/.cc -- header and source files for PostsynapticModels::Custom class
    -- currentSourceModelsCustom.h/.cc -- header and source files for CurrentSourceModels::Custom class
    -- initVarSnippetCustom.h/.cc -- header and source files for InitVarSnippet::Custom class

Example:
    $ python generate_swig_interfaces.py path_to_pygenn

Attrbutes:

    NEURONMODELS -- common name of NeuronModels header and interface files without extention
    POSTSYNMODELS -- common name of PostsynapticModels header and interface files without extention
    WUPDATEMODELS -- common name of WeightUpdateModels header and interface files without extention
    CURRSOURCEMODELS -- common name of CurrentSourceModels header and interface files without extention
    INITVARSNIPPET -- common name of InitVarSnippet header and interface files without extention
    NNMODEL -- common name of NNmodel header and interface files without extention
    MAIN_MODULE -- name of the main SWIG module
    INDIR -- include directory of GeNN
    SWIGDIR = SWIG directory
"""
import os  # to work with paths nicely
from string import Template # for better text substitutions
from argparse import ArgumentParser # to parse command line args

# module attributes
NEURONMODELS = 'newNeuronModels'
POSTSYNMODELS = 'newPostsynapticModels'
WUPDATEMODELS = 'newWeightUpdateModels'
CURRSOURCEMODELS = 'currentSourceModels'
INITVARSNIPPET = 'initVarSnippet'
NNMODEL = 'modelSpec'
MAIN_MODULE = 'pygenn'
INDIR = 'include'
SWIGDIR = 'swig'

# Scope classes should be used with 'with' statement. They write code in the
# beginning and in the end of the with-block.
class SwigInlineScope( object ):
    def __init__( self, ofs ):
        '''Adds %inline block. The code within %inline %{ %} block is added to the generated wrapper C++ file AND is processed by SWIG'''
        self.os = ofs
    def __enter__( self ):
        self.os.write( '\n%inline %{\n' )
    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.write( '%}\n' )

class SwigExtendScope( object ):
    def __init__( self, ofs, classToExtend ):
        '''Adds %extend block. The code within %extend classToExtend { } block is used to add functionality to classes without touching the original implementation'''
        self.os = ofs
        self.classToExtend = classToExtend
    def __enter__( self ):
        self.os.write( '\n%extend ' + self.classToExtend + ' {\n' )
    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.write( '};\n' )

class SwigAsIsScope( object ):
    def __init__( self, ofs ):
        '''Adds %{ %} block. The code within %{ %} block is added to the wrapper C++ file as is without being processed by SWIG'''
        self.os = ofs
    def __enter__( self ):
        self.os.write( '\n%{\n' )
    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.write( '%}\n' )

class SwigInitScope( object ):
    def __init__( self, ofs ):
        '''Adds %init %{ %} block. The code within %{ %} block is copied directly into the module initialization function'''
        self.os = ofs
    def __enter__( self ):
        self.os.write( '\n%init %{\n' )
    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.write( '%}\n' )

class CppBlockScope( object ):
    def __init__( self, ofs ):
        '''Adds a C-style block'''
        self.os = ofs
    def __enter__( self ):
        self.os.write( '\n{\n' )
    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.write( '}\n' )

class SwigModuleGenerator( object ):

    '''A helper class for generating SWIG interface files'''

    def __init__( self, moduleName, outFile ):
        '''Init SwigModuleGenerator

        Arguments:
            moduleName -- string, name of the SWIG module
            outFile -- string, output file
        '''
        self.name = moduleName
        self.outFile = outFile

    # __enter__ and __exit__ are functions which are called if the class is created
    # using 'with' statement. __enter__ is called in the very beginning, and
    # __exit__ is called when the indented with-block ends.
    def __enter__(self):
        self.os = open( self.outFile, 'w' )
        return self

    def __exit__( self, exc_type, exc_value, traceback ):
        self.os.close()

    def addSwigModuleHeadline( self, directors=False, comment='' ):
        '''Adds a line naming a module and enabling directors feature for inheritance in python (disabled by default)'''
        directorsCode = ''
        if directors:
            directorsCode = '(directors="1")'
        self.write( '%module{} {} {}\n'.format( directorsCode, self.name, comment ) )

    def addSwigFeatureDirector( self, cClassName, comment='' ):
        '''Adds a line enabling director feature for a C/C++ class'''
        self.write( '%feature("director") {}; {}\n'.format( cClassName, comment ) )

    def addSwigInclude( self, cHeader, comment='' ):
        '''Adds a line for including C/C++ header file. %include statement makes SWIG to include the header into generated C/C++ file code AND to process it'''
        self.write( '%include {} {}\n'.format( cHeader, comment ) )

    def addSwigImport( self, swigIFace, comment='' ):
        '''Adds a line for importing SWIG interface file. %import statement notifies SWIG about class(es) covered in another module'''
        self.write( '%import {} {}\n'.format( swigIFace, comment ) )

    def addSwigIgnore( self, identifier, comment='' ):
        '''Adds a line instructing SWIG to ignore the matching identifier'''
        self.write( '%ignore {}; {}\n'.format( identifier, comment ) )

    def addSwigRename( self, identifier, newName, comment='' ):
        '''Adds a line instructing SWIG to rename the matching identifier'''
        self.write( '%rename({}) {}; {}\n'.format( newName, identifier, comment ) )

    def addSwigUnignore( self, identifier ):
        '''Adds a line instructing SWIG to unignore the matching identifier'''
        self.addSwigRename( identifier, '"%s"', '// unignore' )

    def addSwigIgnoreAll( self ):
        '''Adds a line instructing SWIG to ignore everything, but templates. Unignoring templates does not work well in SWIG. Can be used for fine-grained control over what is wrapped'''
        self.addSwigRename( '""', '"$ignore"', '// ignore all' )

    def addSwigUnignoreAll( self ):
        '''Adds a line instructing SWIG to unignore everything'''
        self.addSwigRename( '""', '"%s"', '// unignore all' )

    def addSwigTemplate( self, tSpec, newName ):
        '''Adds a template specification tSpec and renames it as newName'''
        self.write( '%template({}) {};\n'.format( newName, tSpec ) )

    def addCppInclude( self, cHeader, comment='' ):
        '''Adds a line for usual including C/C++ header file.'''
        self.write( '#include {} {}\n'.format( cHeader, comment ) )

    def addAutoGenWarning( self ):
        '''Adds a comment line telling that this file was generated automatically'''
        self.write( '// This code was generated by ' + os.path.split(__file__)[1] + '. DO NOT EDIT\n' )

    def write( self, code ):
        '''Writes code into output stream os'''
        self.os.write( code )


def writeValueMakerFunc( modelName, valueName, numValues, mg ):
    '''Generates a helper make*Values function and writes it'''

    vals = 'vals'
    if numValues == 0:
        vals = ''
    paramType = 'double'
    if valueName == 'VarValues':
        paramType = 'NewModels::VarInit'

    mg.write( 'static {0}::{1}::{2}* make{2}( const std::vector<{3}> & {4} )'.format(
        mg.name,
        modelName,
        valueName,
        paramType,
        vals
        ) )
    with CppBlockScope( mg ):
        if numValues != 0:
            mg.write( paramType + ' ' + ', '.join( ['v{0} = vals[{0}]'.format( i )
                                          for i in range(numValues)] ) + ';\n' )
        mg.write( 'return new {}::{}({});\n'.format(
            mg.name + '::' + modelName,
            valueName,
            ', '.join( [('v' + str( i )) for i in range(numValues)] ) ) )


def generateCustomClassDeclaration( nSpace, initVarSnippet=False ):
    '''Generates nSpace::Custom class declaration string'''

    varValuesTypedef = ''
    varValuesMaker = ''
    if not initVarSnippet:
        varValuesTypedef = 'typedef CustomValues::VarValues VarValues;'
        varValuesMaker = '''static CustomValues::VarValues* makeVarValues( const std::vector< NewModels::VarInit > & vals )
        {
            return new CustomValues::VarValues( vals );
        }'''

    return Template('''
namespace ${NAMESPACE}
{
class Custom : public Base
{
private:
    static ${NAMESPACE}::Custom *s_Instance;
public:
    static const ${NAMESPACE}::Custom *getInstance()
    {
        if ( s_Instance == NULL )
        {
            s_Instance = new ${NAMESPACE}::Custom;
        }
        return s_Instance;
    }
    typedef CustomValues::ParamValues ParamValues;
    ${varValuesTypedef}
    static CustomValues::ParamValues* makeParamValues( const std::vector< double > & vals )
    {
        return new CustomValues::ParamValues( vals );
    }
    ${varValuesMaker}
};
} // namespace ${NAMESPACE}
''').substitute(NAMESPACE=nSpace, varValuesTypedef=varValuesTypedef, varValuesMaker=varValuesMaker)

def generateNumpyApplyArgoutviewArray1D( dataType, varName, sizeName ):
    '''Generates a line which applies numpy ARGOUTVIEW_ARRAY1 typemap to variable. ARGOUTVIEW_ARRAY1 gives access to C array via numpy array.'''
    return Template( '%apply ( ${data_t}* ARGOUTVIEW_ARRAY1, int* DIM1 ) {( ${data_t}* ${varName}, int* ${sizeName} )};\n').substitute( data_t=dataType, varName=varName, sizeName=sizeName )

def generateNumpyApplyInArray1D( dataType, varName, sizeName ):
    '''Generates a line which applies numpy IN_ARRAY1 typemap to variable. IN_ARRAY1 is used to pass a numpy array as C array to C code'''
    return Template( '%apply ( ${data_t} IN_ARRAY1, int DIM1 ) {( ${data_t} ${varName}, int ${sizeName} )};\n').substitute( data_t=dataType, varName=varName, sizeName=sizeName )

def generateBuiltInGetter( models ):
    return Template('''std::vector< std::string > getBuiltInModels() {
    return std::vector<std::string>{"${MODELS}"};
}
''').substitute( MODELS='", "'.join( models ) )


def generateSharedLibraryModelInterface( gennPath ):
    '''Generates SharedLibraryModel.i file'''
    with SwigModuleGenerator('SharedLibraryModel',
            os.path.join( gennPath, SWIGDIR, 'SharedLibraryModel.i' ) ) as mg:
        mg.addAutoGenWarning()
        mg.addSwigModuleHeadline()
        with SwigAsIsScope( mg ):
            mg.write( '#define SWIG_FILE_WITH_INIT // for numpy\n' )
            mg.addCppInclude( '"SharedLibraryModel.h"' )

        mg.addSwigInclude( '<std_string.i>' )
        mg.addSwigInclude( '"numpy.i"' )
        mg.write( '%numpy_typemaps(long double, NPY_LONGDOUBLE, int) ')
        with SwigInitScope( mg ):
            mg.write( 'import_array();\n')

        # These are all data types supported by numpy SWIG interface (at least by default) plus long double
        npDTypes = (
                'signed char', 'unsigned char',
                'short', 'unsigned short',
                'int', 'unsigned int',
                'long', 'unsigned long',
                'long long', 'unsigned long long',
                'float', 'double', 'long double'
        )

        for dataType in [dt+'*' for dt in npDTypes]:
            mg.write( generateNumpyApplyArgoutviewArray1D( dataType, 'varPtr', 'n1' ) )
        mg.write( generateNumpyApplyInArray1D( 'unsigned int*', '_ind', 'nConn' ) )
        mg.write( generateNumpyApplyInArray1D( 'unsigned int*', '_indInG', 'nPre' ) )
        mg.write( generateNumpyApplyInArray1D( 'double*', '_g', 'nG' ) )
        mg.write( generateNumpyApplyInArray1D( 'float*', '_g', 'nG' ) )

        mg.addSwigInclude( '"SharedLibraryModel.h"' )
        for dtShort, dataType in zip( [ "".join([dt_[0] for dt_ in dt.split()]) for dt in npDTypes],
                npDTypes ):
            mg.addSwigTemplate( 'SharedLibraryModel::assignExternalPointerArray<{}>'.format( dataType ),
                'assignExternalPointerArray_' + dtShort )
            mg.addSwigTemplate( 'SharedLibraryModel::assignExternalPointerSingle<{}>'.format( dataType ),
                'assignExternalPointerSingle_' + dtShort )

        for dtShort, dataType in zip( ('f', 'd', 'ld'), ('float', 'double', 'long double') ):
            mg.addSwigTemplate( 'SharedLibraryModel<{}>'.format( dataType ),
                'SharedLibraryModel_' + dtShort )


def generateStlContainersInterface( gennPath ):
    '''Generates StlContainers interface which wraps std::string, std::pair,
       std::vector, std::function and creates template specializations for pairs and vectors'''
    with SwigModuleGenerator( 'StlContainers',
            os.path.join( gennPath, SWIGDIR, 'StlContainers.i' ) ) as mg:
        mg.addAutoGenWarning()
        mg.addSwigModuleHeadline()
        with SwigAsIsScope( mg ):
            mg.addCppInclude( '<functional>', '// for std::function' )


        mg.write( '\n// swig wrappers for STL containers\n' )
        mg.addSwigInclude( '<std_string.i>' )
        mg.addSwigInclude( '<std_pair.i>' )
        mg.addSwigInclude( '<std_vector.i>' )

        dpfunc_template_spec = 'function<double( const std::vector<double> &, double )>'
        mg.write( '\n// wrap std::function in a callable struct with the same name\n' )
        mg.write( '// and enable directors feature for it, so that a new class can\n' )
        mg.write( '// be derived from it in python. swig magic.\n')
        mg.addSwigRename( 'std::' + dpfunc_template_spec,
                'STD_DPFunc' )
        mg.addSwigRename(
                'std::' + dpfunc_template_spec + '::operator()',
                '__call__',
                '// rename operator() as __call__ so that it works correctly in python' )
        mg.addSwigFeatureDirector( 'std::' + dpfunc_template_spec )
        mg.write('namespace std{\n'
                 '    struct ' + dpfunc_template_spec + ' {\n'
                 '        // copy ctor\n' +
                 '        {0}( const std::{0}&);\n'.format( dpfunc_template_spec ) +
                 '        double operator()( const std::vector<double> &, double ) const;\n'
                 '    };\n'
                 '}\n' )

        mg.write( '\n// add template specifications for various STL containers\n' )
        mg.addSwigTemplate( 'std::pair<std::string, std::string>', 'StringPair' )
        mg.addSwigTemplate( 'std::pair<std::string, double>', 'StringDoublePair' )
        mg.addSwigTemplate( 'std::pair<std::string, std::pair<std::string, double>>',
                'StringStringDoublePairPair' )
        mg.addSwigTemplate( 'std::pair<std::string, std::{}>'.format( dpfunc_template_spec ),
                'StringDPFPair')
        mg.addSwigTemplate( 'std::vector<std::string>', 'StringVector' )
        mg.addSwigTemplate( 'std::vector<std::pair<std::string, std::string>>', 'StringPairVector' )
        mg.addSwigTemplate( 'std::vector<std::pair<std::string, std::pair<std::string, double>>>',
                'StringStringDoublePairPairVector' )
        mg.addSwigTemplate( 'std::vector<std::pair<std::string, std::{}>>'.format( dpfunc_template_spec ),
                'StringDPFPairVector' )

        # These are all data types supported by numpy SWIG interface (at least by default) plus long double
        npDTypes = (
                'signed char', 'unsigned char',
                'short', 'unsigned short',
                'int', 'unsigned int',
                'long', 'unsigned long',
                'long long', 'unsigned long long',
                'float', 'double', 'long double'
        )

        for npDType in npDTypes:
            camelDT = ''.join( x.capitalize() for x in npDType.split() ) + 'Vector'
            mg.addSwigTemplate( 'std::vector<{}>'.format(npDType), camelDT )


def generateCustomModelDeclImpls( gennPath ):
    '''Generates headers/sources with *::Custom classes'''
    models = [NEURONMODELS, POSTSYNMODELS, WUPDATEMODELS, CURRSOURCEMODELS, INITVARSNIPPET]
    for model in models:
        nSpace = model[:]
        if model.startswith('new'):
            nSpace = nSpace[3:]
        else:
            nSpace = nSpace[0].upper() + nSpace[1:]
        with SwigModuleGenerator( 'decl',
                os.path.join( gennPath, SWIGDIR, model + 'Custom.h' ) ) as mg:
            mg.addAutoGenWarning()
            mg.write( '#pragma once\n' )
            mg.addCppInclude( '"' + model + '.h"' )
            mg.addCppInclude( '"customParamValues.h"' )
            if model != INITVARSNIPPET:
                mg.addCppInclude( '"customVarValues.h"' )
            mg.write(generateCustomClassDeclaration(nSpace, model==INITVARSNIPPET))
        with SwigModuleGenerator( 'impl',
                os.path.join( gennPath, SWIGDIR, model + 'Custom.cc' ) ) as mg:
            mg.addAutoGenWarning()
            mg.addCppInclude( '"' + model + 'Custom.h"' )
            if model != INITVARSNIPPET:
                mg.write('IMPLEMENT_MODEL({}::Custom);\n'.format(nSpace))
            else:
                mg.write('IMPLEMENT_SNIPPET({}::Custom);\n'.format(nSpace))


def generateConfigs( gennPath ):
    '''Generates SWIG interfaces'''
    generateStlContainersInterface( gennPath )
    generateCustomModelDeclImpls( gennPath )
    generateSharedLibraryModelInterface( gennPath )
    includePath = os.path.join( gennPath, INDIR )
    swigPath = os.path.join( gennPath, SWIGDIR )


    # open header files with models and instantiate SwigModuleGenerators
    with open( os.path.join( includePath, NEURONMODELS + ".h" ), 'r' ) as neuronModels_h, \
            open( os.path.join( includePath, POSTSYNMODELS + ".h" ), 'r' ) as postsynModels_h, \
            open( os.path.join( includePath, WUPDATEMODELS + ".h" ), 'r' ) as wUpdateModels_h, \
            open( os.path.join( includePath, CURRSOURCEMODELS + ".h" ), 'r' ) as currSrcModels_h, \
            open( os.path.join( includePath, INITVARSNIPPET + ".h" ), 'r' ) as initVarSnippet_h, \
            SwigModuleGenerator( MAIN_MODULE, os.path.join( swigPath, MAIN_MODULE + '.i' ) ) as pygennSmg, \
            SwigModuleGenerator( 'NeuronModels', os.path.join( swigPath, 'NeuronModels.i' ) ) as neuronSmg, \
            SwigModuleGenerator( 'PostsynapticModels', os.path.join( swigPath, 'PostsynapticModels.i' ) ) as postsynSmg, \
            SwigModuleGenerator( 'WeightUpdateModels', os.path.join( swigPath, 'WeightUpdateModels.i' ) ) as wUpdateSmg, \
            SwigModuleGenerator( 'CurrentSourceModels', os.path.join( swigPath, 'CurrentSourceModels.i' ) ) as currSrcSmg, \
            SwigModuleGenerator( 'InitVarSnippet', os.path.join( swigPath, 'InitVarSnippet.i' ) ) as iniVarSmg:

        # pygennSmg generates main SWIG interface file,
        # mgs generate SWIG interfaces for models and InitVarSnippet

        mgs = [ neuronSmg, postsynSmg, wUpdateSmg, currSrcSmg, iniVarSmg ]

        pygennSmg.addAutoGenWarning()
        pygennSmg.addSwigModuleHeadline()
        with SwigAsIsScope( pygennSmg ):
            pygennSmg.addCppInclude( '"variableMode.h"' )
            pygennSmg.addCppInclude( '"global.h"' )
            pygennSmg.addCppInclude( '"modelSpec.h"' )
            pygennSmg.addCppInclude( '"generateALL.h"' )
            pygennSmg.addCppInclude( '"synapseMatrixType.h"' )
            pygennSmg.addCppInclude( '"neuronGroup.h"' )
            pygennSmg.addCppInclude( '"synapseGroup.h"' )
            pygennSmg.addCppInclude( '"currentSource.h"' )

            pygennSmg.addCppInclude( '"neuronGroup.h"' )
            pygennSmg.addCppInclude( '"synapseGroup.h"' )
            pygennSmg.addCppInclude( '"currentSource.h"' )
            pygennSmg.addCppInclude( '"' + NNMODEL + '.h"' )
            for header in (NEURONMODELS, POSTSYNMODELS,
                           WUPDATEMODELS, CURRSOURCEMODELS, INITVARSNIPPET):
                pygennSmg.addCppInclude( '"' + header + 'Custom.h"' )

        pygennSmg.addSwigImport( '"swig/StlContainers.i"' )

        # do initialization when module is loaded
        with SwigInitScope( pygennSmg ):
            pygennSmg.write( '''
            initGeNN();
            GENN_PREFERENCES::buildSharedLibrary = true;\n
            GENN_PREFERENCES::autoInitSparseVars = true;\n
            #ifdef DEBUG
                GENN_PREFERENCES::optimizeCode = false;
                GENN_PREFERENCES::debugCode = true;
            #endif // DEBUG

            #ifndef CPU_ONLY
                CHECK_CUDA_ERRORS(cudaGetDeviceCount(&deviceCount));
                deviceProp = new cudaDeviceProp[deviceCount];
                for (int device = 0; device < deviceCount; device++) {
                    CHECK_CUDA_ERRORS(cudaSetDevice(device));
                    CHECK_CUDA_ERRORS(cudaGetDeviceProperties(&(deviceProp[device]), device));
                }
            #endif // CPU_ONLY
            ''' )

        # define and wrap two functions which replace main in generateALL.cc
        with SwigInlineScope( pygennSmg ):
            pygennSmg.write( '''
            int initMPI_pygenn() {
                int localHostID = 0;

            #ifdef MPI_ENABLE
                    MPI_Init(NULL, NULL);
                    MPI_Comm_rank(MPI_COMM_WORLD, &localHostID);
                    cout << "MPI initialized - host ID:" << localHostID << endl;
            #endif
                return localHostID;
            }
            void generate_model_runner_pygenn( NNmodel & model, const std::string &path, int localHostID ) {

                if (!model.isFinalized()) {
                gennError("Model was not finalized in modelDefinition(). Please call model.finalize().");
                    }

                #ifndef CPU_ONLY
                    chooseDevice(model, path, localHostID);
                #endif // CPU_ONLY
                    generate_model_runner(model, path, localHostID);

                #ifdef MPI_ENABLE
                    MPI_Finalize();
                    cout << "MPI finalized." << endl;
                #endif
            }
            ''' )

        # generate SWIG interface files for models and InitVarSnippet
        for mg, header in zip(mgs, (neuronModels_h, postsynModels_h,
                                   wUpdateModels_h, currSrcModels_h, initVarSnippet_h)):
            _, headerFilename = os.path.split( header.name )
            pygennSmg.addSwigImport( '"swig/' + mg.name + '.i"' )
            mg.addAutoGenWarning()
            mg.addSwigModuleHeadline( directors = True )
            with SwigAsIsScope( mg ):
                mg.addCppInclude( '"' + headerFilename + '"' )
                mg.addCppInclude( '"' + headerFilename.split('.')[0] + 'Custom.h"' )
                mg.addCppInclude( '"../swig/customParamValues.h"' )
                if mg.name != 'InitVarSnippet':
                    mg.addCppInclude( '"initVarSnippetCustom.h"' )
                    mg.addCppInclude( '"../swig/customVarValues.h"' )

            if mg.name == 'InitVarSnippet':
                mg.addSwigImport( '"Snippet.i"' )
            else:
                mg.addSwigIgnore( 'LegacyWrapper' )
                mg.addSwigImport( '"NewModels.i"' )
            mg.addSwigFeatureDirector( mg.name + '::Base' )
            mg.addSwigInclude( '"include/' + headerFilename + '"' )
            mg.addSwigFeatureDirector( mg.name + '::Custom' )
            mg.addSwigInclude( '"' + headerFilename.split('.')[0] + 'Custom.h"' )

            mg.models = []

            # parse models files and collect models declared there
            for line in header.readlines():
                line = line.lstrip()
                if line.startswith( 'DECLARE_' ):
                    if mg.name == 'InitVarSnippet':
                        nspace_model_name, num_params = line.split( '(' )[1].split( ')' )[0].split( ',' )
                    else:
                        nspace_model_name, num_params, num_vars = line.split( '(' )[1].split( ')' )[0].split( ',' )
                    if mg.name == 'NeuronModels' or mg.name == 'InitVarSnippet':
                        model_name = nspace_model_name.split( '::' )[1]
                    else:
                        model_name = nspace_model_name

                    num_params = int( num_params)
                    num_vars = int( num_vars )

                    mg.models.append( model_name )

                    # add a helper function to create Param- and VarVarlues to each model
                    with SwigExtendScope( mg, mg.name + '::' + model_name ):
                        writeValueMakerFunc( model_name, 'ParamValues', num_params, mg )
                        if mg.name != 'InitVarSnippet':
                            writeValueMakerFunc( model_name, 'VarValues', num_vars, mg )

        # wrap NeuronGroup, SynapseGroup and CurrentSource
        pygennSmg.addSwigInclude( '"include/neuronGroup.h"' )
        pygennSmg.addSwigInclude( '"include/synapseGroup.h"' )
        pygennSmg.addSwigInclude( '"include/currentSource.h"' )

        with SwigAsIsScope( pygennSmg ):
            for mg in mgs:
                mg.models.append( 'Custom' )

        # wrap modelSpec.h
        # initGeNN is called in the initialization block so no need to wrap it
        pygennSmg.addSwigIgnore( 'initGeNN' )
        pygennSmg.addSwigIgnore( 'GeNNReady' )
        pygennSmg.addSwigIgnore( 'SynapseConnType' )
        pygennSmg.addSwigIgnore( 'SynapseGType' )
        pygennSmg.addSwigInclude( '"include/' + NNMODEL + '.h"' )

        # the next three for-loop create template specializations to add
        # various populations to NNmodel
        for n_model in mgs[0].models:
            pygennSmg.addSwigTemplate(
                'NNmodel::addNeuronPopulation<{}::{}>'.format( mgs[0].name, n_model ),
                'addNeuronPopulation_{}'.format( n_model ) )

        for ps_model in mgs[1].models:
            for wu_model in mgs[2].models:

                pygennSmg.addSwigTemplate(
                    'NNmodel::addSynapsePopulation<{}::{}, {}::{}>'.format(
                        mgs[2].name, wu_model, mgs[1].name, ps_model ),
                    'addSynapsePopulation_{}_{}'.format( wu_model, ps_model ) )

        for cs_model in mgs[3].models:
            pygennSmg.addSwigTemplate(
                'NNmodel::addCurrentSource<{}::{}>'.format( mgs[3].name, cs_model ),
                'addCurrentSource_{}'.format( cs_model ) )

        # creates template specializations for initVar function
        for ivsnippet in mgs[4].models:
            pygennSmg.addSwigTemplate(
                'initVar<{}::{}>'.format( mgs[4].name, ivsnippet ),
                'initVar_{}'.format( ivsnippet ) )

        pygennSmg.write( '\n// wrap variables from global.h. Note that GENN_PREFERENCES is\n' )
        pygennSmg.write( '// already covered in the GeNNPreferences.i interface\n' )
        pygennSmg.addSwigIgnore( 'GENN_PREFERENCES' )
        pygennSmg.addSwigIgnore( 'deviceProp' )
        pygennSmg.addSwigInclude( '"include/global.h"' )

        pygennSmg.write( '\n// wrap variableMode.h.\n' )
        pygennSmg.addSwigIgnore( 'operator&' )
        pygennSmg.addSwigInclude( '"include/variableMode.h"' )

        pygennSmg.write( '\n// wrap synapseMatrixType.h\n' )
        pygennSmg.addSwigInclude( '"include/synapseMatrixType.h"' )
        with SwigInlineScope( pygennSmg ):
            pygennSmg.write( 'void setDefaultVarMode( const VarMode &varMode ) {\n' )
            pygennSmg.write( '  GENN_PREFERENCES::defaultVarMode = varMode;\n}' )
        pygennSmg.addSwigImport( '"swig/GeNNPreferences.i"' )


# if the module is called directly i.e. as $ python generate_swig_interfaces.py
if __name__ == '__main__':

    parser = ArgumentParser( description='Generate SWIG interfaces' )
    parser.add_argument(
            'genn_path', metavar='DIR', type=str, help='Path to GeNN')

    gennPath = parser.parse_args().genn_path

    # check that all required files can be found
    if not os.path.isfile( os.path.join( gennPath, INDIR, NEURONMODELS + '.h' ) ):
        print( 'Error: The {0} file is missing'.format( os.path.join( INDIR, NEURONMODELS + '.h' ) ) )
        exit(1)

    if not os.path.isfile( os.path.join( gennPath, INDIR, POSTSYNMODELS + '.h' ) ):
        print( 'Error: The {0} file is missing'.format( os.path.join( INDIR, POSTSYNMODELS + '.h' ) ) )
        exit(1)

    if not os.path.isfile( os.path.join(gennPath, INDIR, WUPDATEMODELS + '.h' ) ):
        print( 'Error: The {0} file is missing'.format( os.path.join( INDIR, WUPDATEMODELS + '.h' ) ) )
        exit(1)

    if not os.path.isfile( os.path.join(gennPath, INDIR, CURRSOURCEMODELS + '.h' ) ):
        print( 'Error: The {0} file is missing'.format( os.path.join( INDIR, CURRSOURCEMODELS + '.h' ) ) )
        exit(1)

    if not os.path.isfile( os.path.join(gennPath, INDIR, NNMODEL + '.h' ) ):
        print( 'Error: The {0} file is missing'.format( os.path.join( INDIR, NNMODEL + '.h' ) ) )
        exit(1)

    generateConfigs( gennPath )
